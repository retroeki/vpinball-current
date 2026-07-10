// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayRecorder.h"
#include "ReplayData.h"

#include "plugins/MsgPlugin.h"
#include "core/VPXPluginAPIImpl.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <cfloat>
#include <cmath>

ReplayRecorder::ReplayRecorder(Player *player, const string &finalPath)
   : m_player(player)
   , m_finalPath(finalPath)
   , m_tmpPath(finalPath + ".tmp")
{
   BuildReplayStateRegistry(player, m_stateFields);
   m_lastState.assign(m_stateFields.size(), 0.f);
   // Every chunk starts with a keyframe holding a full state snapshot; make sure one always fits
   m_chunkCapacity = std::max(CHUNK_SIZE, (size_t)(m_stateFields.size() * 4 + 16 * 1024));
   m_cur.data.reserve(m_chunkCapacity);

   // Open the session temp file up front with a placeholder header; sealed
   // chunks stream into it as play proceeds. Finalize patches the header and
   // renames over m_finalPath, so a crash mid-session leaves only a .tmp whose
   // zeroed header fails validation.
   try
   {
      std::filesystem::create_directories(std::filesystem::path(m_finalPath).parent_path());
   }
   catch (const std::exception &e)
   {
      PLOGE << "ReplayRecorder: failed to create directory for " << m_finalPath << ": " << e.what();
   }
   m_file.open(m_tmpPath, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
   if (m_file.is_open())
   {
      const uint16_t version = FORMAT_VERSION;
      const uint16_t flags = 0; // bit 0 reserved for compression (Phase 1)
      const uint32_t zero = 0;
      m_file.write("VPRP", 4);
      m_file.write(reinterpret_cast<const char *>(&version), 2);
      m_file.write(reinterpret_cast<const char *>(&flags), 2);
      m_file.write(reinterpret_cast<const char *>(&zero), 4); // firstMs, patched at finalize
      m_file.write(reinterpret_cast<const char *>(&zero), 4); // lastMs
      m_file.write(reinterpret_cast<const char *>(&zero), 4); // chunkCount
      m_fileOk = !m_file.fail();
   }
   if (m_fileOk)
      m_writerThread = std::thread(&ReplayRecorder::WriterLoop, this);
   else
      PLOGE << "ReplayRecorder: failed to open " << m_tmpPath << ", session will not be recorded";

   const MsgPluginAPI *msgApi = &MsgPI::MsgPluginManager::GetInstance().GetMsgAPI();
   const uint32_t endpoint = VPXPluginAPIImpl::GetInstance().GetVPXEndPointId();
   m_onDisplayChgId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_ON_SRC_CHG_MSG);
   m_getDisplayId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG);
   m_onSegChgId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_ON_SRC_CHG_MSG);
   m_getSegId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_GET_SRC_MSG);
   msgApi->SubscribeMsg(endpoint, m_onDisplayChgId, OnSourcesChanged, this);
   msgApi->SubscribeMsg(endpoint, m_onSegChgId, OnSourcesChanged, this);

   PLOGI << "ReplayRecorder: enabled (full session -> " << m_tmpPath << ", ball sample " << BALL_SAMPLE_INTERVAL_MS << "ms, " << m_stateFields.size() << " state fields)";
}

ReplayRecorder::~ReplayRecorder()
{
   const MsgPluginAPI *msgApi = &MsgPI::MsgPluginManager::GetInstance().GetMsgAPI();
   msgApi->UnsubscribeMsg(m_onDisplayChgId, OnSourcesChanged);
   msgApi->UnsubscribeMsg(m_onSegChgId, OnSourcesChanged);
   msgApi->ReleaseMsgID(m_onDisplayChgId);
   msgApi->ReleaseMsgID(m_getDisplayId);
   msgApi->ReleaseMsgID(m_onSegChgId);
   msgApi->ReleaseMsgID(m_getSegId);

   // Seal the open chunk, drain the writer, finalize the file. Player nulls its
   // pointer before deleting, so no new records arrive past this point.
   {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_curOpen && !m_cur.data.empty())
      {
         std::lock_guard<std::mutex> ql(m_queueMutex);
         m_queue.push_back(std::move(m_cur));
      }
      m_curOpen = false;
   }
   {
      std::lock_guard<std::mutex> ql(m_queueMutex);
      m_writerStop = true;
   }
   m_queueCv.notify_one();
   if (m_writerThread.joinable())
      m_writerThread.join();
   FinalizeFile();
}

void ReplayRecorder::WriterLoop()
{
   SetThreadName("ReplayWriter"s);
   for (;;)
   {
      Chunk c;
      {
         std::unique_lock<std::mutex> lock(m_queueMutex);
         m_queueCv.wait(lock, [this] { return m_writerStop || !m_queue.empty(); });
         if (m_queue.empty())
            return; // only reachable when stopping with a drained queue
         c = std::move(m_queue.front());
         m_queue.pop_front();
      }
      AppendChunkToFile(c);
   }
}

void ReplayRecorder::AppendChunkToFile(const Chunk &c)
{
   if (!m_fileOk)
      return;
   const uint32_t size = (uint32_t)c.data.size();
   if (m_bytesWritten + 8 + size > MAX_SESSION_BYTES)
   {
      if (!m_capWarned)
      {
         m_capWarned = true;
         PLOGW << "ReplayRecorder: session cap of " << (MAX_SESSION_BYTES >> 20) << " MB reached, recording truncated";
      }
      return;
   }
   m_file.write(reinterpret_cast<const char *>(&c.firstTimeMs), 4);
   m_file.write(reinterpret_cast<const char *>(&size), 4);
   m_file.write(reinterpret_cast<const char *>(c.data.data()), size);
   if (m_file.fail())
   {
      PLOGE << "ReplayRecorder: write to " << m_tmpPath << " failed (disk full?), session dropped";
      m_fileOk = false;
      return;
   }
   if (m_chunkCount == 0)
      m_firstMs = c.firstTimeMs;
   m_lastMs = c.lastTimeMs;
   m_chunkCount++;
   m_bytesWritten += 8 + size;
}

void ReplayRecorder::FinalizeFile()
{
   if (!m_fileOk || m_chunkCount == 0)
   {
      m_file.close();
      std::error_code ec;
      std::filesystem::remove(m_tmpPath, ec);
      PLOGI << "ReplayRecorder: nothing recorded (or write failed), keeping previous " << m_finalPath;
      return;
   }
   m_file.seekp(8);
   m_file.write(reinterpret_cast<const char *>(&m_firstMs), 4);
   m_file.write(reinterpret_cast<const char *>(&m_lastMs), 4);
   m_file.write(reinterpret_cast<const char *>(&m_chunkCount), 4);
   m_file.flush();
   const bool ok = !m_file.fail();
   m_file.close();
   std::error_code ec;
   if (!ok)
   {
      std::filesystem::remove(m_tmpPath, ec);
      PLOGE << "ReplayRecorder: header finalize failed, session dropped";
      return;
   }
   std::filesystem::rename(m_tmpPath, m_finalPath, ec); // atomically replaces the previous session
   if (ec)
   {
      PLOGE << "ReplayRecorder: rename to " << m_finalPath << " failed: " << ec.message();
      std::filesystem::remove(m_tmpPath, ec);
      return;
   }
   PLOGI << "ReplayRecorder: session recording " << m_finalPath << " (" << m_bytesWritten << " bytes, " << m_chunkCount << " chunks, " << (m_lastMs - m_firstMs) << "ms)";
}

void ReplayRecorder::OnSourcesChanged(const unsigned int msgId, void *userData, void *msgData)
{
   // May fire on any thread (e.g. FlexDMD teardown): just flag, OnFrame re-enumerates
   static_cast<ReplayRecorder *>(userData)->m_sourcesDirty = true;
}

void ReplayRecorder::OnPhysicsCycle()
{
   const uint32_t t = m_player->m_time_msec;

   if (!m_flippersBound)
   {
      m_flippers = m_player->m_physics->GetFlippers();
      m_lastFlipperAngle.assign(m_flippers.size(), FLT_MAX);
      m_flippersBound = true;
   }

   std::lock_guard<std::mutex> lock(m_mutex);

   // Flipper angles: written per 1 ms cycle, but only while actually moving
   for (size_t i = 0; i < m_flippers.size(); i++)
   {
      const float angle = m_flippers[i]->m_flipperMover.m_angleCur;
      if (fabsf(angle - m_lastFlipperAngle[i]) > 1e-4f)
      {
         m_lastFlipperAngle[i] = angle;
         EnsureSpace(1 + 4 + 1 + 4, t);
         PutU8(REC_FLIPPER);
         PutU32(t);
         PutU8((uint8_t)i);
         PutF32(angle);
      }
   }

   // Ball transforms on a fixed 8 ms grid (device and frame-rate independent)
   if ((t / BALL_SAMPLE_INTERVAL_MS) != (m_lastBallSampleMs / BALL_SAMPLE_INTERVAL_MS) && !m_player->m_vball.empty())
   {
      m_lastBallSampleMs = t;
      const size_t n = std::min(m_player->m_vball.size(), (size_t)255);
      EnsureSpace(1 + 4 + 1 + n * (1 + 12 + 8), t);
      PutU8(REC_BALL_SAMPLE);
      PutU32(t);
      PutU8((uint8_t)n);
      for (size_t i = 0; i < n; i++)
      {
         HitBall *const pball = m_player->m_vball[i];
         float q[4];
         MatrixToQuat(pball, q);
         PutU8(BallId(pball));
         PutF32(pball->m_d.m_pos.x);
         PutF32(pball->m_d.m_pos.y);
         PutF32(pball->m_d.m_pos.z);
         PutQuat(q);
      }
   }

   if (m_curOpen)
      m_cur.lastTimeMs = t;
}

void ReplayRecorder::OnFrame()
{
   const uint32_t t = m_player->m_time_msec;
   std::lock_guard<std::mutex> lock(m_mutex);

   if (!m_stateInit)
   { // baseline; the first keyframe carries the full snapshot, so no diff records needed yet
      for (size_t i = 0; i < m_stateFields.size(); i++)
         m_lastState[i] = GetReplayStateField(m_stateFields[i]);
      m_stateInit = true;
      return;
   }

   for (size_t i = 0; i < m_stateFields.size(); i++)
   {
      const float v = GetReplayStateField(m_stateFields[i]);
      if (v != m_lastState[i])
      {
         m_lastState[i] = v;
         EnsureSpace(1 + 4 + 2 + 4, t);
         PutU8(REC_STATE);
         PutU32(t);
         PutU16((uint16_t)i);
         PutF32(v);
      }
   }

   if (m_sourcesDirty.exchange(false))
      EnumerateSources(t);
   PollDisplaySources(t);

   if (m_curOpen)
      m_cur.lastTimeMs = t;
}

void ReplayRecorder::EnumerateSources(uint32_t timeMs)
{
   const MsgPluginAPI *msgApi = &MsgPI::MsgPluginManager::GetInstance().GetMsgAPI();
   const uint32_t endpoint = VPXPluginAPIImpl::GetInstance().GetVPXEndPointId();

   GetDisplaySrcMsg gd = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpoint, m_getDisplayId, &gd);
   m_displaySources.resize(gd.count);
   if (gd.count)
   {
      gd = { gd.count, 0, m_displaySources.data() };
      msgApi->BroadcastMsg(endpoint, m_getDisplayId, &gd);
      m_displaySources.resize(std::min((size_t)gd.count, m_displaySources.size()));
   }
   for (const DisplaySrcId &src : m_displaySources)
   {
      if (m_displayKeys.find(src.id.id) == m_displayKeys.end())
      {
         const uint8_t key = (uint8_t)m_displayKeys.size();
         m_displayKeys[src.id.id] = key;
         m_lastDisplayFrameId[key] = 0xFFFFFFFFu;
         WriteDisplayDef(timeMs, key, src);
         PLOGI << "ReplayRecorder: display source " << (int)key << ": " << src.width << 'x' << src.height << " format " << src.frameFormat;
      }
   }

   GetSegSrcMsg gs = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpoint, m_getSegId, &gs);
   m_segSources.resize(gs.count);
   if (gs.count)
   {
      gs = { gs.count, 0, m_segSources.data() };
      msgApi->BroadcastMsg(endpoint, m_getSegId, &gs);
      m_segSources.resize(std::min((size_t)gs.count, m_segSources.size()));
   }
   for (const SegSrcId &src : m_segSources)
   {
      if (m_segKeys.find(src.id.id) == m_segKeys.end())
      {
         const uint8_t key = (uint8_t)m_segKeys.size();
         m_segKeys[src.id.id] = key;
         m_lastSegFrameId[key] = 0xFFFFFFFFu;
         WriteSegDef(timeMs, key, src);
         PLOGI << "ReplayRecorder: seg source " << (int)key << ": " << src.nElements << " elements";
      }
   }
}

void ReplayRecorder::PollDisplaySources(uint32_t timeMs)
{
   for (const DisplaySrcId &src : m_displaySources)
   {
      const auto keyIt = m_displayKeys.find(src.id.id);
      if (keyIt == m_displayKeys.end() || src.GetRenderFrame == nullptr)
         continue;
      const uint8_t key = keyIt->second;
      const DisplayFrame f = src.GetRenderFrame(src.id);
      if (f.frame == nullptr || f.frameId == m_lastDisplayFrameId[key])
         continue;
      m_lastDisplayFrameId[key] = f.frameId;

      const size_t nPix = (size_t)src.width * src.height;
      if (src.frameFormat == CTLPI_DISPLAY_FORMAT_LUM32F)
      {
         m_frameScratch.resize(nPix);
         const float *lum = static_cast<const float *>(f.frame);
         for (size_t i = 0; i < nPix; i++)
            m_frameScratch[i] = (uint8_t)(std::clamp(lum[i], 0.f, 1.f) * 255.f);
      }
      else if (src.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB888)
      {
         m_frameScratch.resize(nPix * 3);
         memcpy(m_frameScratch.data(), f.frame, nPix * 3);
      }
      else if (src.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB565)
      {
         m_frameScratch.resize(nPix * 2);
         memcpy(m_frameScratch.data(), f.frame, nPix * 2);
      }
      else
         continue;

      m_lastDisplayPayload[key] = m_frameScratch;
      WriteDisplayFrame(timeMs, key, m_frameScratch);
   }

   for (const SegSrcId &src : m_segSources)
   {
      const auto keyIt = m_segKeys.find(src.id.id);
      if (keyIt == m_segKeys.end() || src.GetState == nullptr)
         continue;
      const uint8_t key = keyIt->second;
      const SegDisplayFrame f = src.GetState(src.id);
      if (f.frame == nullptr || f.frameId == m_lastSegFrameId[key])
         continue;
      m_lastSegFrameId[key] = f.frameId;

      const size_t n = (size_t)std::min(src.nElements, (unsigned int)CTLPI_SEG_MAX_DISP_ELEMENTS) * 16;
      m_frameScratch.resize(n);
      for (size_t i = 0; i < n; i++)
         m_frameScratch[i] = (uint8_t)(std::clamp(f.frame[i], 0.f, 1.f) * 255.f);

      m_lastSegPayload[key] = m_frameScratch;
      WriteSegFrame(timeMs, key, m_frameScratch);
   }
}

void ReplayRecorder::WriteDisplayDef(uint32_t timeMs, uint8_t key, const DisplaySrcId &src)
{
   EnsureSpace(1 + 4 + 1 + 8 + 8 + 8 + 2 + 2 + 4 + 1, timeMs);
   PutU8(REC_DISPLAY_DEF);
   PutU32(timeMs);
   PutU8(key);
   PutU64(src.id.id);
   PutU64(src.groupId.id);
   PutU64(src.overrideId.id);
   PutU16((uint16_t)src.width);
   PutU16((uint16_t)src.height);
   PutU32(src.hardware);
   PutU8((uint8_t)src.frameFormat);
}

void ReplayRecorder::WriteSegDef(uint32_t timeMs, uint8_t key, const SegSrcId &src)
{
   const uint8_t n = (uint8_t)std::min(src.nElements, (unsigned int)CTLPI_SEG_MAX_DISP_ELEMENTS);
   EnsureSpace(1 + 4 + 1 + 8 + 8 + 4 + 1 + n, timeMs);
   PutU8(REC_SEG_DEF);
   PutU32(timeMs);
   PutU8(key);
   PutU64(src.id.id);
   PutU64(src.groupId.id);
   PutU32(src.hardware);
   PutU8(n);
   for (uint8_t i = 0; i < n; i++)
      PutU8((uint8_t)src.elementType[i]);
}

void ReplayRecorder::WriteDisplayFrame(uint32_t timeMs, uint8_t key, const std::vector<uint8_t> &raw)
{
   ReplayData::RleEncode(raw.data(), raw.size(), m_rleScratch);
   const bool rle = m_rleScratch.size() < raw.size();
   const std::vector<uint8_t> &payload = rle ? m_rleScratch : raw;
   EnsureSpace(1 + 4 + 1 + 1 + 4 + payload.size(), timeMs);
   PutU8(REC_DISPLAY_FRAME);
   PutU32(timeMs);
   PutU8(key);
   PutU8(rle ? 1 : 0);
   PutU32((uint32_t)payload.size());
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + payload.size());
   memcpy(m_cur.data.data() + o, payload.data(), payload.size());
}

void ReplayRecorder::WriteSegFrame(uint32_t timeMs, uint8_t key, const std::vector<uint8_t> &raw)
{
   ReplayData::RleEncode(raw.data(), raw.size(), m_rleScratch);
   const bool rle = m_rleScratch.size() < raw.size();
   const std::vector<uint8_t> &payload = rle ? m_rleScratch : raw;
   EnsureSpace(1 + 4 + 1 + 1 + 2 + payload.size(), timeMs);
   PutU8(REC_SEG_FRAME);
   PutU32(timeMs);
   PutU8(key);
   PutU8(rle ? 1 : 0);
   PutU16((uint16_t)payload.size());
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + payload.size());
   memcpy(m_cur.data.data() + o, payload.data(), payload.size());
}

void ReplayRecorder::EmitSourceDefsAndFrames(uint32_t timeMs)
{
   // Called right after a fresh chunk's keyframe: re-emit defs and the latest
   // payloads so playback can enter at any retained chunk. The chunk was just
   // opened, so appends here intentionally skip EnsureSpace (no reentrancy).
   for (const DisplaySrcId &src : m_displaySources)
   {
      const auto keyIt = m_displayKeys.find(src.id.id);
      if (keyIt == m_displayKeys.end())
         continue;
      WriteDisplayDef(timeMs, keyIt->second, src);
      const auto payload = m_lastDisplayPayload.find(keyIt->second);
      if (payload != m_lastDisplayPayload.end() && !payload->second.empty())
         WriteDisplayFrame(timeMs, keyIt->second, payload->second);
   }
   for (const SegSrcId &src : m_segSources)
   {
      const auto keyIt = m_segKeys.find(src.id.id);
      if (keyIt == m_segKeys.end())
         continue;
      WriteSegDef(timeMs, keyIt->second, src);
      const auto payload = m_lastSegPayload.find(keyIt->second);
      if (payload != m_lastSegPayload.end() && !payload->second.empty())
         WriteSegFrame(timeMs, keyIt->second, payload->second);
   }
}

void ReplayRecorder::OnPlaySound(VPX::Sound *sound, float volumeOffset, float randomPitch, int pitch, float panOffset, float frontRearFadeOffset, int loopcount, bool useSame, bool restart)
{
   const uint32_t t = m_player->m_time_msec;
   const std::string &name = sound->GetName();
   const uint8_t nameLen = (uint8_t)std::min(name.size(), (size_t)255);
   std::lock_guard<std::mutex> lock(m_mutex);
   EnsureSpace(1 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 1 + 1 + nameLen, t);
   PutU8(REC_SOUND);
   PutU32(t);
   PutF32(volumeOffset);
   PutF32(randomPitch);
   PutU32((uint32_t)pitch);
   PutF32(panOffset);
   PutF32(frontRearFadeOffset);
   PutU32((uint32_t)loopcount);
   PutU8((useSame ? 1 : 0) | (restart ? 2 : 0));
   PutU8(nameLen);
   for (uint8_t i = 0; i < nameLen; i++)
      PutU8((uint8_t)name[i]);
}

void ReplayRecorder::OnStopSound(VPX::Sound *sound)
{
   const uint32_t t = m_player->m_time_msec;
   const std::string &name = sound->GetName();
   const uint8_t nameLen = (uint8_t)std::min(name.size(), (size_t)255);
   std::lock_guard<std::mutex> lock(m_mutex);
   EnsureSpace(1 + 4 + 1 + nameLen, t);
   PutU8(REC_SOUND_STOP);
   PutU32(t);
   PutU8(nameLen);
   for (uint8_t i = 0; i < nameLen; i++)
      PutU8((uint8_t)name[i]);
}

void ReplayRecorder::OnRomAudio(const AudioUpdateMsg &msg)
{
   const uint32_t t = m_player->m_time_msec;
   const bool isFloat = (msg.format == CTLPI_AUDIO_FORMAT_SAMPLE_FLOAT);
   const int channels = (msg.type == CTLPI_AUDIO_SRC_BACKGLASS_MONO) ? 1 : 2;
   const size_t bytesPerSample = isFloat ? 4 : 2;
   const size_t nFrames = msg.bufferSize / (bytesPerSample * channels);
   if (nFrames < 2)
      return;
   const uint16_t outRate = (uint16_t)std::min((double)UINT16_MAX, msg.sampleRate * 0.5);

   std::lock_guard<std::mutex> lock(m_mutex);

   // Downmix to mono s16 at half rate: average 2 frames x channels per output sample
   const size_t outCount = nFrames / 2;
   m_audioScratch.resize(outCount);
   const float invN = 1.f / (float)(2 * channels);
   for (size_t i = 0; i < outCount; i++)
   {
      float acc = 0.f;
      for (size_t s = 0; s < 2 * (size_t)channels; s++)
      {
         const size_t idx = i * 2 * channels + s;
         acc += isFloat ? reinterpret_cast<const float *>(msg.buffer)[idx] * 32767.f : (float)reinterpret_cast<const int16_t *>(msg.buffer)[idx];
      }
      m_audioScratch[i] = (int16_t)std::clamp(acc * invN, -32768.f, 32767.f);
   }

   uint8_t key;
   const auto it = m_audioKeys.find(msg.id.id);
   if (it != m_audioKeys.end())
      key = it->second;
   else
   {
      key = (uint8_t)m_audioKeys.size();
      m_audioKeys[msg.id.id] = key;
   }

   // Split into records of at most 4096 samples
   for (size_t off = 0; off < outCount; off += 4096)
   {
      const uint16_t count = (uint16_t)std::min(outCount - off, (size_t)4096);
      EnsureSpace(1 + 4 + 1 + 2 + 4 + 2 + (size_t)count * 2, t);
      PutU8(REC_ROM_AUDIO);
      PutU32(t);
      PutU8(key);
      PutU16(outRate);
      PutF32(msg.volume);
      PutU16(count);
      const size_t o = m_cur.data.size();
      m_cur.data.resize(o + (size_t)count * 2);
      memcpy(m_cur.data.data() + o, m_audioScratch.data() + off, (size_t)count * 2);
   }
}

void ReplayRecorder::OnBallCreated(HitBall *pHitBall)
{
   const uint32_t t = m_player->m_time_msec;
   std::lock_guard<std::mutex> lock(m_mutex);
   EnsureSpace(1 + 4 + 1 + 4 + 4 + 12, t);
   PutU8(REC_BALL_CREATE);
   PutU32(t);
   PutU8(BallId(pHitBall));
   PutF32(pHitBall->m_d.m_radius);
   PutF32(pHitBall->m_d.m_mass);
   PutF32(pHitBall->m_d.m_pos.x);
   PutF32(pHitBall->m_d.m_pos.y);
   PutF32(pHitBall->m_d.m_pos.z);
}

void ReplayRecorder::OnBallDestroyed(HitBall *pHitBall)
{
   const uint32_t t = m_player->m_time_msec;
   std::lock_guard<std::mutex> lock(m_mutex);
   const auto it = m_ballIds.find(pHitBall);
   if (it == m_ballIds.end())
      return;
   EnsureSpace(1 + 4 + 1, t);
   PutU8(REC_BALL_DESTROY);
   PutU32(t);
   PutU8(it->second);
   m_ballIds.erase(it);
}

void ReplayRecorder::OpenChunk(uint32_t timeMs)
{
   if (m_curOpen && !m_cur.data.empty())
   {
      m_cur.lastTimeMs = timeMs;
      { // hand the sealed chunk to the writer thread; O(1) move, no I/O here
         std::lock_guard<std::mutex> ql(m_queueMutex);
         m_queue.push_back(std::move(m_cur));
      }
      m_queueCv.notify_one();
      m_cur = Chunk {};
   }
   m_cur.data.reserve(m_chunkCapacity);
   m_cur.firstTimeMs = timeMs;
   m_cur.lastTimeMs = timeMs;
   m_curOpen = true;
   m_emittingKeyframe = true; // EnsureSpace must not re-open the chunk we are seeding
   WriteKeyframe(timeMs);
   EmitSourceDefsAndFrames(timeMs);
   m_emittingKeyframe = false;
}

void ReplayRecorder::EnsureSpace(size_t bytes, uint32_t timeMs)
{
   if (m_emittingKeyframe)
      return; // fresh chunk being seeded; the vector grows as needed
   if (bytes + 4096 > m_chunkCapacity)
      m_chunkCapacity = bytes + 16 * 1024; // single oversized record (e.g. big RGB display frame)
   if (!m_curOpen || m_cur.data.size() + bytes > m_chunkCapacity)
      OpenChunk(timeMs);
}

void ReplayRecorder::WriteKeyframe(uint32_t timeMs)
{
   // Complete snapshot so any chunk is a valid playback entry point
   const size_t nBalls = std::min(m_player->m_vball.size(), (size_t)255);
   const size_t nFlippers = std::min(m_flippers.size(), (size_t)255);
   PutU8(REC_KEYFRAME);
   PutU32(timeMs);
   PutU8((uint8_t)nBalls);
   for (size_t i = 0; i < nBalls; i++)
   {
      HitBall *const pball = m_player->m_vball[i];
      float q[4];
      MatrixToQuat(pball, q);
      PutU8(BallId(pball));
      PutF32(pball->m_d.m_radius);
      PutF32(pball->m_d.m_pos.x);
      PutF32(pball->m_d.m_pos.y);
      PutF32(pball->m_d.m_pos.z);
      PutQuat(q);
   }
   PutU8((uint8_t)nFlippers);
   for (size_t i = 0; i < nFlippers; i++)
   {
      PutU8((uint8_t)i);
      PutF32(m_flippers[i]->m_flipperMover.m_angleCur);
   }
   // Full state snapshot so this chunk is a valid entry point; also the diff baseline
   PutU16((uint16_t)m_stateFields.size());
   for (size_t i = 0; i < m_stateFields.size(); i++)
   {
      const float v = GetReplayStateField(m_stateFields[i]);
      m_lastState[i] = v;
      PutF32(v);
   }
   m_stateInit = true;
}

uint8_t ReplayRecorder::BallId(HitBall *pHitBall)
{
   const auto it = m_ballIds.find(pHitBall);
   if (it != m_ballIds.end())
      return it->second;
   const uint8_t id = m_nextBallId++;
   m_ballIds[pHitBall] = id;
   return id;
}

void ReplayRecorder::PutU8(uint8_t v) { m_cur.data.push_back(v); }

void ReplayRecorder::PutU16(uint16_t v)
{
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + 2);
   memcpy(m_cur.data.data() + o, &v, 2);
}

void ReplayRecorder::PutU64(uint64_t v)
{
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + 8);
   memcpy(m_cur.data.data() + o, &v, 8);
}

void ReplayRecorder::PutU32(uint32_t v)
{
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + 4);
   memcpy(m_cur.data.data() + o, &v, 4);
}

void ReplayRecorder::PutF32(float v)
{
   const size_t o = m_cur.data.size();
   m_cur.data.resize(o + 4);
   memcpy(m_cur.data.data() + o, &v, 4);
}

void ReplayRecorder::PutQuat(const float q[4])
{
   for (int i = 0; i < 4; i++)
   {
      const int16_t s = (int16_t)(std::clamp(q[i], -1.f, 1.f) * 32767.f);
      const size_t o = m_cur.data.size();
      m_cur.data.resize(o + 2);
      memcpy(m_cur.data.data() + o, &s, 2);
   }
}

void ReplayRecorder::MatrixToQuat(const HitBall *pHitBall, float q[4])
{
   // Standard orthonormal 3x3 to quaternion (w, x, y, z), Shepperd's method
   const Matrix3 &m = pHitBall->m_orientation;
   const float trace = m._11 + m._22 + m._33;
   if (trace > 0.f)
   {
      const float s = 0.5f / sqrtf(trace + 1.0f);
      q[0] = 0.25f / s;
      q[1] = (m._32 - m._23) * s;
      q[2] = (m._13 - m._31) * s;
      q[3] = (m._21 - m._12) * s;
   }
   else if (m._11 > m._22 && m._11 > m._33)
   {
      const float s = 2.0f * sqrtf(1.0f + m._11 - m._22 - m._33);
      q[0] = (m._32 - m._23) / s;
      q[1] = 0.25f * s;
      q[2] = (m._12 + m._21) / s;
      q[3] = (m._13 + m._31) / s;
   }
   else if (m._22 > m._33)
   {
      const float s = 2.0f * sqrtf(1.0f + m._22 - m._11 - m._33);
      q[0] = (m._13 - m._31) / s;
      q[1] = (m._12 + m._21) / s;
      q[2] = 0.25f * s;
      q[3] = (m._23 + m._32) / s;
   }
   else
   {
      const float s = 2.0f * sqrtf(1.0f + m._33 - m._11 - m._22);
      q[0] = (m._21 - m._12) / s;
      q[1] = (m._13 + m._31) / s;
      q[2] = (m._23 + m._32) / s;
      q[3] = 0.25f * s;
   }
}
