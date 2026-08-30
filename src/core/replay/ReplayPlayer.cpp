// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayPlayer.h"

#include "core/VPXPluginAPIImpl.h"

#include "imgui/imgui.h"
#include "miniaudio/miniaudio.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>

ReplayPlayer *ReplayPlayer::s_instance = nullptr;

ReplayPlayer::ReplayPlayer(Player *player, const std::string &path)
   : m_player(player)
{
   m_loaded = m_data.Load(path);
   if (m_loaded)
   {
      BuildReplayStateRegistry(player, m_stateFields);
      for (VPX::Sound *const sound : player->m_ptable->m_vsound)
         m_soundsByName[sound->GetName()] = sound;
      if (m_stateFields.size() != m_data.GetStateFieldCount())
         PLOGW << "ReplayPlayer: state registry mismatch (table has " << m_stateFields.size() << " fields, recording has " << m_data.GetStateFieldCount()
               << "), was the table modified since recording? Applying the overlap only";

      // Build the live display/segment buffers and publish them on the plugin bus
      for (const auto &entry : m_data.GetDisplayDefs())
      {
         LiveDisplay d;
         d.def = entry.second;
         const size_t nPix = (size_t)d.def.width * d.def.height;
         if (d.def.frameFormat == CTLPI_DISPLAY_FORMAT_LUM32F)
            d.lumBuf.assign(nPix, 0.f);
         else
            d.byteBuf.assign(nPix * (d.def.frameFormat == CTLPI_DISPLAY_FORMAT_SRGB888 ? 3 : 2), 0);
         m_liveDisplays.emplace(entry.first, std::move(d));
      }
      for (const auto &entry : m_data.GetSegDefs())
      {
         LiveSeg s;
         s.def = entry.second;
         m_liveSegs.emplace(entry.first, std::move(s));
      }
      s_instance = this;
      SetupDisplayPublishing();

      // Export configuration (all via Settings so the app needs no new JNI)
      const Settings &settings = player->m_ptable->m_settings;
      m_exportMode = settings.GetStandalone_ReplayExportMode();
      const int fps = std::clamp(settings.GetStandalone_ReplayExportFps(), 24, 120);
      m_stepMs = 1000.0 / (double)fps;
      const int startMs = std::max(0, settings.GetStandalone_ReplayExportStartMs());
      const int lenMs = std::max(0, settings.GetStandalone_ReplayExportLenMs());
      m_windowStartMs = m_data.GetFirstMs() + (uint32_t)startMs;
      m_windowEndMs = lenMs > 0 ? std::min(m_data.GetLastMs(), m_windowStartMs + (uint32_t)lenMs) : m_data.GetLastMs();
      if (m_windowStartMs >= m_windowEndMs)
      { // bad window: fall back to the whole recording
         m_windowStartMs = m_data.GetFirstMs();
         m_windowEndMs = m_data.GetLastMs();
      }
      // The PinMAME ROM slider is a dB gain that live playback applies through
      // the audio engine (Player::UpdateVolume -> SetPluginStreamGain). The
      // export mixer bypasses the engine entirely, so read it here and apply it
      // by hand. Otherwise a user who raised the slider to fix quiet ROM audio
      // hears the boost in-game and nothing at all in their exported clip.
      m_romExportGain = powf(10.0f, (float)settings.GetPlayer_PinmameVolume() / 20.0f);

      if (m_exportMode)
      {
         PLOGI << "ReplayPlayer: EXPORT mode, window " << m_windowStartMs << ".." << m_windowEndMs << "ms at " << fps << "fps (clean render; the app burns overlays)";
         DumpExportAudio(g_pvp->GetPrefPath() + "replays" + PATH_SEPARATOR_CHAR + "export-audio.wav");
      }

      PLOGI << "ReplayPlayer: playback of " << path << " (" << m_data.GetBalls().size() << " balls, " << m_data.GetFlippers().size() << " flippers, "
            << m_data.GetStateEvents().size() << " state events, " << m_liveDisplays.size() << " displays, " << m_liveSegs.size() << " seg displays, "
            << m_data.GetDurationMs() << "ms loop)";
   }
   else
   {
      PLOGW << "ReplayPlayer: could not load " << path << ", nothing will be played back";
   }
}

ReplayPlayer::~ReplayPlayer()
{
   // Balls created through Player::CreateBall are owned and cleaned up by the Player teardown
   TeardownDisplayPublishing();
   if (s_instance == this)
      s_instance = nullptr;
   CloseAudioStreams();
}

namespace
{
constexpr uint32_t MIX_RATE = 44100;

// Live, a ROM stream is mono and miniaudio duplicates it to both speakers, so
// what a listener hears is 2x the sample value; the recorder meanwhile stores
// the AVERAGE of the source channels (ReplayRecorder::OnRomAudio). Table SFX
// are already placed at their live two-speaker sum by the busGain below, so
// without this factor ROM audio lands exactly 6 dB under everything else --
// the "ROM is quiet, table sounds are fine" report.
constexpr float ROM_MONO_SPREAD = 2.f;

// One scheduled playback of a table sound, reconstructed from the recorded
// PlaySound/StopSound events. Volume changes without restart (the standard
// music-ducking pattern: PlaySound with usesame and no restart) become gain
// automation points so the music never pops.
struct SfxInstance
{
   VPX::Sound *sound;
   uint32_t startT;
   int pitch;     // Hz offset added to the native rate, like SoundPlayer::Play
   int plays;     // -1 = loop forever, else total number of plays
   uint32_t cutT; // hard cut from StopSound / usesame+restart, UINT32_MAX if none
   std::vector<std::pair<uint32_t, float>> gains; // (clip ms, 0..1 volume)
};

bool DecodeSoundMono(const VPX::Sound *sound, std::vector<float> &pcm, uint32_t &nativeRate)
{
   ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0); // mono float at the file's native rate
   cfg.channelMixMode = ma_channel_mix_mode_simple;
   ma_decoder dec;
   if (ma_decoder_init_memory(sound->GetFileRaw(), sound->GetFileSize(), &cfg, &dec) != MA_SUCCESS)
      return false;
   nativeRate = dec.outputSampleRate;
   pcm.clear();
   float buf[16384];
   for (;;)
   {
      ma_uint64 read = 0;
      const ma_result res = ma_decoder_read_pcm_frames(&dec, buf, 16384, &read);
      pcm.insert(pcm.end(), buf, buf + read);
      if (res != MA_SUCCESS || read < 16384)
         break;
   }
   ma_decoder_uninit(&dec);
   return nativeRate > 0 && !pcm.empty();
}

uint64_t QuerySoundFrames(const VPX::Sound *sound, uint32_t &nativeRate)
{
   ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
   cfg.channelMixMode = ma_channel_mix_mode_simple;
   ma_decoder dec;
   if (ma_decoder_init_memory(sound->GetFileRaw(), sound->GetFileSize(), &cfg, &dec) != MA_SUCCESS)
      return 0;
   nativeRate = dec.outputSampleRate;
   ma_uint64 frames = 0;
   if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) != MA_SUCCESS || frames == 0)
   { // format without a cheap length: count by decoding (rare)
      float buf[16384];
      for (;;)
      {
         ma_uint64 read = 0;
         const ma_result res = ma_decoder_read_pcm_frames(&dec, buf, 16384, &read);
         frames += read;
         if (res != MA_SUCCESS || read < 16384)
            break;
      }
   }
   ma_decoder_uninit(&dec);
   return nativeRate > 0 ? frames : 0;
}
}

void ReplayPlayer::DumpExportAudio(const std::string &path) const
{
   // Assemble the export window's FULL soundtrack into a mono s16 WAV at
   // 44.1 kHz so the host can encode and mux it without parsing .vpr:
   //  - table sound effects and music (recorded PlaySound/StopSound events,
   //    mixed offline from the table's own sound files — original tables like
   //    Goonies VPW have no ROM stream, ALL their audio lives here), and
   //  - ROM audio (PinMAME music/callouts), every recorded stream.
   // Loop state is tracked from the clip start so music that began before the
   // export window still sounds inside it. Known limit: a loop started before
   // the clip's first chunk is not in the clip at all and stays silent.
   const uint32_t ws = m_windowStartMs, we = m_windowEndMs;
   const size_t mixLen = (size_t)(((uint64_t)(we - ws)) * MIX_RATE / 1000);

   // --- Reconstruct sound instances from the event stream (clip start -> window end)
   struct SoundInfo
   {
      uint64_t frames = 0;
      uint32_t nativeRate = 0;
   };
   std::unordered_map<VPX::Sound *, SoundInfo> infos;
   const auto &events = m_data.GetSoundEvents();
   const auto &stops = m_data.GetSoundStops();
   std::vector<SfxInstance> instances;
   std::unordered_map<VPX::Sound *, std::vector<size_t>> active;

   auto info = [&](VPX::Sound *s) -> SoundInfo & {
      auto it = infos.find(s);
      if (it == infos.end())
      {
         SoundInfo fresh;
         fresh.frames = QuerySoundFrames(s, fresh.nativeRate);
         it = infos.emplace(s, fresh).first;
      }
      return it->second;
   };
   auto playMs = [&](const SfxInstance &ins) -> double { // duration of ONE play, after pitch
      const SoundInfo &si = infos[ins.sound];
      const double effRate = (double)si.nativeRate + (double)ins.pitch;
      return (si.frames == 0 || effRate <= 0.0) ? 0.0 : 1000.0 * (double)si.frames / effRate;
   };
   auto naturalEndMs = [&](const SfxInstance &ins) -> double {
      if (ins.plays < 0)
         return 1e18;
      return (double)ins.startT + (double)ins.plays * playMs(ins);
   };
   auto endMs = [&](const SfxInstance &ins) -> double { return std::min(naturalEndMs(ins), ins.cutT == UINT32_MAX ? 1e18 : (double)ins.cutT); };

   size_t ei = 0, si = 0;
   while (ei < events.size() || si < stops.size())
   {
      const bool takeStop = si < stops.size() && (ei >= events.size() || stops[si].t < events[ei].t);
      const uint32_t t = takeStop ? stops[si].t : events[ei].t;
      if (t > we)
         break;
      if (takeStop)
      {
         const auto it = m_soundsByName.find(stops[si].name);
         if (it != m_soundsByName.end())
         {
            for (const size_t idx : active[it->second])
               if (endMs(instances[idx]) > (double)t)
                  instances[idx].cutT = t;
            active[it->second].clear();
         }
         si++;
         continue;
      }
      const ReplayData::SoundEvent &e = events[ei++];
      const auto it = m_soundsByName.find(e.name);
      if (it == m_soundsByName.end())
         continue;
      VPX::Sound *const sound = it->second;
      const SoundInfo &sinfo = info(sound);
      if (sinfo.frames == 0)
         continue;
      const float vol = std::clamp(dequantizeSignedPercent(sound->GetVolume()) + e.volumeOffset, 0.f, 1.f);
      auto &act = active[sound];
      // drop finished instances from the active list
      act.erase(std::remove_if(act.begin(), act.end(), [&](size_t idx) { return endMs(instances[idx]) <= (double)t; }), act.end());
      if (e.useSame && !act.empty())
      {
         SfxInstance &cur = instances[act.back()];
         if (e.restart)
            cur.cutT = t; // cut, then fall through to start a fresh instance
         else
         { // live path re-applies settings without restarting: gain automation
            cur.gains.emplace_back(t, vol);
            continue;
         }
      }
      SfxInstance ins;
      ins.sound = sound;
      ins.startT = t;
      ins.pitch = e.pitch;
      ins.plays = e.loopcount < 0 ? -1 : (e.loopcount <= 1 ? 1 : e.loopcount);
      ins.cutT = UINT32_MAX;
      ins.gains.emplace_back(t, vol);
      if (e.useSame)
         act.clear();
      act.push_back(instances.size());
      instances.push_back(std::move(ins));
      if (instances.size() > 20000)
      { // runaway safety: keep the mix bounded on pathological clips
         PLOGW << "ReplayPlayer: sound event cap hit, soundtrack truncated";
         break;
      }
   }
   // keep only instances audible inside the window
   instances.erase(std::remove_if(instances.begin(), instances.end(),
                      [&](const SfxInstance &ins) { return endMs(ins) <= (double)ws || (double)ins.startT >= (double)we; }),
      instances.end());

   const auto &blocks = m_data.GetAudioBlocks();
   bool hasRom = false;
   for (const auto &b : blocks)
      if (b.t + 2000 >= ws && b.t <= we)
      {
         hasRom = true;
         break;
      }

   if (instances.empty() && !hasRom)
   {
      std::remove(path.c_str()); // no soundtrack for this window; stale files must not be muxed
      PLOGI << "ReplayPlayer: no audio in the export window";
      return;
   }

   std::vector<float> mix(mixLen, 0.f);

   // --- Sound effects: decode each unique sound once, place all its instances, free
   std::unordered_map<VPX::Sound *, std::vector<size_t>> bySound;
   for (size_t i = 0; i < instances.size(); i++)
      bySound[instances[i].sound].push_back(i);
   for (const auto &entry : bySound)
   {
      std::vector<float> pcm;
      uint32_t nativeRate = 0;
      if (!DecodeSoundMono(entry.first, pcm, nativeRate))
         continue;
      // Match the live SoundPlayer loudness: sqrt volume curve, and backglass
      // sounds carry the stereo-decode compensation factor.
      const float busGain = (entry.first->GetOutputTarget() == VPX::SNDOUT_BACKGLASS) ? 2.f : 1.f;
      for (const size_t idx : entry.second)
      {
         const SfxInstance &ins = instances[idx];
         const double step = ((double)nativeRate + (double)ins.pitch) / (double)MIX_RATE; // src frames per out sample
         if (step <= 0.0)
            continue;
         const double lenOut = (double)pcm.size() / step; // out samples of one play
         if (lenOut < 1.0)
            continue;
         const double insEndOut = (endMs(ins) - (double)ws) * MIX_RATE / 1000.0;
         const double cutOut = ins.cutT == UINT32_MAX ? 1e18 : ((double)ins.cutT - (double)ws) * MIX_RATE / 1000.0;
         const double startOut = ((double)ins.startT - (double)ws) * MIX_RATE / 1000.0;
         const long firstK = startOut < 0.0 ? (long)(-startOut / lenOut) : 0;
         for (long k = firstK; ins.plays < 0 || k < ins.plays; k++)
         {
            const double occStart = startOut + (double)k * lenOut;
            if (occStart >= (double)mixLen || occStart >= insEndOut)
               break;
            const size_t from = (size_t)std::max(0.0, occStart);
            const size_t to = (size_t)std::max(0.0, std::min({ (double)mixLen, occStart + lenOut, insEndOut }));
            size_t gi = 0;
            for (size_t i = from; i < to; i++)
            {
               const double tMs = (double)ws + (double)i * 1000.0 / MIX_RATE;
               while (gi + 1 < ins.gains.size() && (double)ins.gains[gi + 1].first <= tMs)
                  gi++;
               float gain = busGain * sqrtf(ins.gains[gi].second);
               const double toCut = cutOut - (double)i; // short fade on hard cuts to avoid clicks
               if (toCut < MIX_RATE / 200.0)
                  gain *= (float)std::max(0.0, toCut / (MIX_RATE / 200.0));
               const double src = ((double)i - occStart) * step;
               const size_t s0 = (size_t)src;
               if (s0 + 1 >= pcm.size())
                  break;
               const float frac = (float)(src - (double)s0);
               mix[i] += gain * (pcm[s0] * (1.f - frac) + pcm[s0 + 1] * frac);
            }
         }
      }
   }

   // --- ROM audio: every stream, sequential per key with block timestamps
   // re-anchoring on drift, resampled to the mix rate, block volume applied
   // Match what a listener actually hears live: the two-speaker spread the
   // recorder averaged away, and the PinMAME dB slider the audio engine would
   // have applied.
   const float romGain = ROM_MONO_SPREAD * m_romExportGain;
   std::map<uint8_t, double> romPos; // next out-sample position per stream key
   for (const auto &b : blocks)
   {
      if (b.t > we || b.rate == 0 || b.samples.size() < 2)
         continue;
      const double blockDurMs = 1000.0 * (double)b.samples.size() / (double)b.rate;
      if ((double)b.t + blockDurMs < (double)ws)
         continue;
      const double anchor = ((double)b.t - (double)ws) * MIX_RATE / 1000.0;
      auto it = romPos.find(b.key);
      double pos = (it == romPos.end() || std::abs(anchor - it->second) > 30.0 * MIX_RATE / 1000.0) ? anchor : it->second;
      const double step = (double)b.rate / (double)MIX_RATE;
      const double nOut = (double)b.samples.size() / step;
      const size_t from = (size_t)std::max(0.0, pos);
      const size_t to = (size_t)std::max(0.0, std::min((double)mixLen, pos + nOut));
      for (size_t i = from; i < to; i++)
      {
         const double src = ((double)i - pos) * step;
         const size_t s0 = (size_t)src;
         if (s0 + 1 >= b.samples.size())
            break;
         const float frac = (float)(src - (double)s0);
         mix[i] += romGain * b.volume * ((float)b.samples[s0] * (1.f - frac) + (float)b.samples[s0 + 1] * frac) * (1.f / 32768.f);
      }
      romPos[b.key] = pos + nOut;
   }

   // --- Normalize: never boost, only pull an overshooting mix down to -1 dBFS
   float peak = 0.f;
   for (const float v : mix)
      peak = std::max(peak, std::abs(v));
   const float norm = peak > 0.891f ? 0.891f / peak : 1.f;

   std::vector<int16_t> pcm(mixLen);
   for (size_t i = 0; i < mixLen; i++)
      pcm[i] = (int16_t)std::clamp(mix[i] * norm * 32767.f, -32768.f, 32767.f);

   std::ofstream out(path, std::ios::binary | std::ios::trunc);
   if (!out.is_open())
   {
      PLOGE << "ReplayPlayer: failed to write " << path;
      return;
   }
   const uint32_t rate = MIX_RATE;
   const uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
   const uint32_t chunkSize = 36 + dataBytes;
   const uint16_t fmt = 1 /*PCM*/, channels = 1, bitsPerSample = 16, blockAlign = 2;
   const uint32_t byteRate = rate * 2;
   out.write("RIFF", 4);
   out.write(reinterpret_cast<const char *>(&chunkSize), 4);
   out.write("WAVEfmt ", 8);
   const uint32_t fmtSize = 16;
   out.write(reinterpret_cast<const char *>(&fmtSize), 4);
   out.write(reinterpret_cast<const char *>(&fmt), 2);
   out.write(reinterpret_cast<const char *>(&channels), 2);
   out.write(reinterpret_cast<const char *>(&rate), 4);
   out.write(reinterpret_cast<const char *>(&byteRate), 4);
   out.write(reinterpret_cast<const char *>(&blockAlign), 2);
   out.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
   out.write("data", 4);
   out.write(reinterpret_cast<const char *>(&dataBytes), 4);
   out.write(reinterpret_cast<const char *>(pcm.data()), dataBytes);
   out.close();
   PLOGI << "ReplayPlayer: wrote export soundtrack " << path << " (" << (pcm.size() * 1000 / rate) << "ms at " << rate << "Hz, " << instances.size() << " sfx instances"
         << (hasRom ? ", ROM audio" : "") << (norm < 1.f ? ", normalized" : "") << ')';
}

void ReplayPlayer::SetupDisplayPublishing()
{
   if (m_liveDisplays.empty() && m_liveSegs.empty())
      return;
   const MsgPluginAPI *msgApi = &MsgPI::MsgPluginManager::GetInstance().GetMsgAPI();
   const uint32_t endpoint = VPXPluginAPIImpl::GetInstance().GetVPXEndPointId();
   m_getDisplayId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG);
   m_onDisplayChgId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_ON_SRC_CHG_MSG);
   m_getSegId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_GET_SRC_MSG);
   m_onSegChgId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_SEG_ON_SRC_CHG_MSG);
   msgApi->SubscribeMsg(endpoint, m_getDisplayId, OnGetDisplaySrc, this);
   msgApi->SubscribeMsg(endpoint, m_getSegId, OnGetSegSrc, this);
   m_publishing = true;
   // Announce our sources so consumers (ScoreView, DMD views, mirroring) re-enumerate
   msgApi->BroadcastMsg(endpoint, m_onDisplayChgId, nullptr);
   msgApi->BroadcastMsg(endpoint, m_onSegChgId, nullptr);
}

void ReplayPlayer::TeardownDisplayPublishing()
{
   if (!m_publishing)
      return;
   m_publishing = false;
   const MsgPluginAPI *msgApi = &MsgPI::MsgPluginManager::GetInstance().GetMsgAPI();
   const uint32_t endpoint = VPXPluginAPIImpl::GetInstance().GetVPXEndPointId();
   msgApi->UnsubscribeMsg(m_getDisplayId, OnGetDisplaySrc);
   msgApi->UnsubscribeMsg(m_getSegId, OnGetSegSrc);
   msgApi->BroadcastMsg(endpoint, m_onDisplayChgId, nullptr);
   msgApi->BroadcastMsg(endpoint, m_onSegChgId, nullptr);
   msgApi->ReleaseMsgID(m_getDisplayId);
   msgApi->ReleaseMsgID(m_onDisplayChgId);
   msgApi->ReleaseMsgID(m_getSegId);
   msgApi->ReleaseMsgID(m_onSegChgId);
}

void ReplayPlayer::OnGetDisplaySrc(const unsigned int msgId, void *userData, void *msgData)
{
   ReplayPlayer *const me = static_cast<ReplayPlayer *>(userData);
   GetDisplaySrcMsg &msg = *static_cast<GetDisplaySrcMsg *>(msgData);
   for (const auto &entry : me->m_liveDisplays)
   {
      if (msg.count < msg.maxEntryCount)
      {
         DisplaySrcId src = {};
         src.id.id = entry.second.def.id;
         src.groupId.id = entry.second.def.groupId;
         src.overrideId.id = entry.second.def.overrideId;
         src.width = entry.second.def.width;
         src.height = entry.second.def.height;
         src.hardware = entry.second.def.hardware;
         src.frameFormat = entry.second.def.frameFormat;
         src.GetRenderFrame = &GetRenderFrameCb;
         src.identifyFormat = 0;
         src.GetIdentifyFrame = nullptr;
         msg.entries[msg.count] = src;
      }
      msg.count++;
   }
}

void ReplayPlayer::OnGetSegSrc(const unsigned int msgId, void *userData, void *msgData)
{
   ReplayPlayer *const me = static_cast<ReplayPlayer *>(userData);
   GetSegSrcMsg &msg = *static_cast<GetSegSrcMsg *>(msgData);
   for (const auto &entry : me->m_liveSegs)
   {
      if (msg.count < msg.maxEntryCount)
      {
         SegSrcId src = {};
         src.id.id = entry.second.def.id;
         src.groupId.id = entry.second.def.groupId;
         src.hardware = entry.second.def.hardware;
         src.nElements = entry.second.def.nElements;
         for (uint8_t i = 0; i < entry.second.def.nElements; i++)
            src.elementType[i] = (SegElementType)entry.second.def.elementType[i];
         src.GetState = &GetSegStateCb;
         msg.entries[msg.count] = src;
      }
      msg.count++;
   }
}

DisplayFrame MSGPIAPI ReplayPlayer::GetRenderFrameCb(const CtlResId id)
{
   ReplayPlayer *const me = s_instance;
   if (me)
      for (auto &entry : me->m_liveDisplays)
         if (entry.second.def.id == id.id)
            return { entry.second.frameId,
               entry.second.def.frameFormat == CTLPI_DISPLAY_FORMAT_LUM32F ? static_cast<const void *>(entry.second.lumBuf.data()) : static_cast<const void *>(entry.second.byteBuf.data()) };
   static const float s_empty[16 * 16] = {};
   return { 0, s_empty };
}

SegDisplayFrame MSGPIAPI ReplayPlayer::GetSegStateCb(const CtlResId id)
{
   ReplayPlayer *const me = s_instance;
   if (me)
      for (auto &entry : me->m_liveSegs)
         if (entry.second.def.id == id.id)
            return { entry.second.frameId, entry.second.values };
   static const float s_empty[16 * 32] = {};
   return { 0, s_empty };
}

void ReplayPlayer::CloseAudioStreams()
{
   if (m_player->m_audioPlayer)
      for (auto &entry : m_audioStreams)
         m_player->m_audioPlayer->CloseAudioStream(entry.second, false);
   m_audioStreams.clear();
}

void ReplayPlayer::ResetLoop()
{
   for (auto &entry : m_liveBalls)
      if (entry.second.hitBall)
         m_player->DestroyBall(entry.second.hitBall);
   m_liveBalls.clear();
   m_flipperCursors.clear();
   m_stateCursor = 0;
   m_stateSnapshotApplied = false;
   m_soundCursor = 0;
   m_soundStopCursor = 0;
   m_audioCursor = 0;
   m_displayFrameCursor = 0;
   m_segFrameCursor = 0;
   CloseAudioStreams();
}

void ReplayPlayer::ApplyStateSnapshot(const ReplayData::StateSnapshot &snapshot)
{
   const size_t n = std::min(m_stateFields.size(), snapshot.values.size());
   for (size_t i = 0; i < n; i++)
      SetReplayStateField(m_stateFields[i], snapshot.values[i]);
}

void ReplayPlayer::Update()
{
   if (!m_loaded)
      return;

   uint32_t t;
   if (m_exportMode)
   {
      if (m_finished)
         return;
      // Advance exactly once per rendered frame (UpdateGameLogic can run more
      // than once per frame on the BGFX loop), never against the wall clock
      if (m_player->m_overall_frames != m_lastSeenFrame)
      {
         if (m_lastSeenFrame != 0xFFFFFFFFu)
            m_exportAccumMs += m_stepMs;
         m_lastSeenFrame = m_player->m_overall_frames;
      }
      const double exportT = (double)m_windowStartMs + m_exportAccumMs;
      if (exportT >= (double)m_windowEndMs)
      {
         m_finished = true;
         PLOGI << "ReplayPlayer: export window complete (" << (m_windowEndMs - m_windowStartMs) << "ms), stopping session";
         m_player->SetCloseState(Player::CS_STOP_PLAY);
         return;
      }
      t = (uint32_t)exportT;
      if (!m_started)
      { // fast-forward cursors to the window start on the first frame
         m_started = true;
         if (t > m_data.GetFirstMs())
         { /* cursors advance below; events up to t apply in this first pass */ }
      }
   }
   else
   {
      const uint64_t nowUs = usec();
      if (!m_started)
      {
         m_startUs = nowUs;
         m_started = true;
      }
      // Pause resilience: game-logic stalls (the exit-replay dialog's soft
      // pause, the app going to background) would otherwise leap the
      // wall-clock playback time forward on resume. Any inter-frame gap
      // beyond a hitch shifts the clock origin so the replay resumes from
      // the frame it froze on.
      else if (m_lastUpdateUs != 0 && nowUs - m_lastUpdateUs > 250 * 1000)
         m_startUs += nowUs - m_lastUpdateUs - 16000;
      m_lastUpdateUs = nowUs;
      t = m_data.GetFirstMs() + (uint32_t)(((nowUs - m_startUs) / 1000) % m_data.GetDurationMs());
      if (t < m_lastT)
         ResetLoop();
   }
   m_lastT = t;

   // Advance the player clock: part animation (Player::PrepareFrame gates
   // UpdateAnimation on it) and light fading are driven by m_time_msec, which
   // normally advances with physics. On loop wrap the one negative diff frame
   // is skipped by the > 0 guard in PrepareFrame.
   m_player->m_time_msec = t;
   m_player->m_time_sec = (double)t / 1000.0;

   // Script/physics-driven visual state: apply the loop-start snapshot once,
   // then replay change events up to the current time
   if (!m_stateSnapshotApplied && !m_data.GetStateSnapshots().empty())
   {
      ApplyStateSnapshot(m_data.GetStateSnapshots().front());
      m_stateSnapshotApplied = true;
   }
   const auto &stateEvents = m_data.GetStateEvents();
   while (m_stateCursor < stateEvents.size() && stateEvents[m_stateCursor].t <= t)
   {
      const ReplayData::StateEvent &e = stateEvents[m_stateCursor];
      if (e.slot < m_stateFields.size())
         SetReplayStateField(m_stateFields[e.slot], e.value);
      m_stateCursor++;
   }

   // Audible playback is skipped in export mode: the video's audio track is
   // muxed from the recording app-side, and fixed-step rendering is not
   // wall-clock synchronous. Everything visual still runs.
   if (!m_exportMode)
   {
      // Sound effects: re-fire through the live API (recorder is off during playback)
      const auto &soundEvents = m_data.GetSoundEvents();
      while (m_soundCursor < soundEvents.size() && soundEvents[m_soundCursor].t <= t)
      {
         const ReplayData::SoundEvent &e = soundEvents[m_soundCursor++];
         if (m_player->m_PlaySound)
         {
            const auto it = m_soundsByName.find(e.name);
            if (it != m_soundsByName.end())
               m_player->m_audioPlayer->PlaySound(it->second, e.volumeOffset, e.randomPitch, e.pitch, e.panOffset, e.fadeOffset, e.loopcount, e.useSame, e.restart);
         }
      }
      const auto &soundStops = m_data.GetSoundStops();
      while (m_soundStopCursor < soundStops.size() && soundStops[m_soundStopCursor].t <= t)
      {
         const ReplayData::SoundStopEvent &e = soundStops[m_soundStopCursor++];
         const auto it = m_soundsByName.find(e.name);
         if (it != m_soundsByName.end())
            m_player->m_audioPlayer->StopSound(it->second);
      }
   }

   // Display frames (DMD, FlexDMD, colorized overrides): copy into the live
   // buffers our published sources serve to consumers
   const auto &dispFrames = m_data.GetDisplayFrames();
   while (m_displayFrameCursor < dispFrames.size() && dispFrames[m_displayFrameCursor].t <= t)
   {
      const ReplayData::DisplayFrameEvent &e = dispFrames[m_displayFrameCursor++];
      const auto it = m_liveDisplays.find(e.key);
      if (it != m_liveDisplays.end())
      {
         LiveDisplay &d = it->second;
         if (d.def.frameFormat == CTLPI_DISPLAY_FORMAT_LUM32F)
         {
            const size_t n = std::min(d.lumBuf.size(), e.payload.size());
            for (size_t i = 0; i < n; i++)
               d.lumBuf[i] = (float)e.payload[i] * (1.f / 255.f);
         }
         else
            memcpy(d.byteBuf.data(), e.payload.data(), std::min(d.byteBuf.size(), e.payload.size()));
         d.frameId++;
      }
   }

   // Segment display frames (alphanumeric/EM score displays)
   const auto &segFrames = m_data.GetSegFrames();
   while (m_segFrameCursor < segFrames.size() && segFrames[m_segFrameCursor].t <= t)
   {
      const ReplayData::SegFrameEvent &e = segFrames[m_segFrameCursor++];
      const auto it = m_liveSegs.find(e.key);
      if (it != m_liveSegs.end())
      {
         LiveSeg &s = it->second;
         const size_t n = std::min((size_t)16 * 32, e.values.size());
         for (size_t i = 0; i < n; i++)
            s.values[i] = (float)e.values[i] * (1.f / 255.f);
         s.frameId++;
      }
   }

   // ROM audio (music/callouts): enqueue recorded PCM with a little lookahead so
   // the device never starves between frames. Not audible during export.
   if (!m_exportMode)
   {
      const auto &audioBlocks = m_data.GetAudioBlocks();
      while (m_audioCursor < audioBlocks.size() && audioBlocks[m_audioCursor].t <= t + 200)
      {
         const ReplayData::AudioBlock &b = audioBlocks[m_audioCursor++];
         auto it = m_audioStreams.find(b.key);
         if (it == m_audioStreams.end())
         {
            VPX::AudioPlayer::AudioStreamID stream = m_player->m_audioPlayer->OpenAudioStream("Replay.Rom."s + std::to_string(b.key), b.rate, 1, false);
            if (!stream)
               continue;
            it = m_audioStreams.emplace(b.key, stream).first;
         }
         m_player->m_audioPlayer->SetStreamVolume(it->second, b.volume);
         m_player->m_audioPlayer->EnqueueStream(it->second, reinterpret_cast<uint8_t *>(const_cast<int16_t *>(b.samples.data())), (int)(b.samples.size() * 2));
      }
   }

   for (const auto &entry : m_data.GetBalls())
   {
      const uint8_t id = entry.first;
      const ReplayData::BallTrack &track = entry.second;
      const auto &s = track.samples;
      if (s.empty())
         continue;
      LiveBall &live = m_liveBalls[id];

      // Advance cursor so s[cursor] <= t < s[cursor+1]
      while (live.cursor + 1 < s.size() && s[live.cursor + 1].t <= t)
         live.cursor++;
      const ReplayData::BallSample &a = s[live.cursor];

      // Visibility: not yet created, in a destroy/re-create gap, or past its end
      bool visible = a.t <= t;
      float x = a.x, y = a.y, z = a.z;
      float qw = a.qw, qx = a.qx, qy = a.qy, qz = a.qz;
      float vx = 0.f, vy = 0.f, vz = 0.f;
      if (visible && live.cursor + 1 < s.size())
      {
         const ReplayData::BallSample &b = s[live.cursor + 1];
         if (b.t - a.t > ReplayData::INTERP_GAP_MS)
            visible = (t <= a.t + ReplayData::INTERP_GAP_MS);
         else if (b.t > a.t)
         {
            const float f = (float)(t - a.t) / (float)(b.t - a.t);
            x += (b.x - a.x) * f;
            y += (b.y - a.y) * f;
            z += (b.z - a.z) * f;
            // nlerp is fine at 8 ms granularity
            const float dot = a.qw * b.qw + a.qx * b.qx + a.qy * b.qy + a.qz * b.qz;
            const float sign = dot < 0.f ? -1.f : 1.f;
            qw += (sign * b.qw - a.qw) * f;
            qx += (sign * b.qx - a.qx) * f;
            qy += (sign * b.qy - a.qy) * f;
            qz += (sign * b.qz - a.qz) * f;
            // Approximate velocity in VP units per 10 ms, for render effects only
            const float toVpt = 10.f / (float)(b.t - a.t);
            vx = (b.x - a.x) * toVpt;
            vy = (b.y - a.y) * toVpt;
            vz = (b.z - a.z) * toVpt;
         }
      }
      else if (visible && t > a.t + ReplayData::INTERP_GAP_MS)
         visible = false; // past the last sample of this ball

      if (!visible)
      {
         if (live.hitBall)
         {
            m_player->DestroyBall(live.hitBall);
            live.hitBall = nullptr;
         }
         continue;
      }

      if (!live.hitBall)
         live.hitBall = m_player->CreateBall(x, y, z, 0.f, 0.f, 0.f, track.radius, track.mass);

      live.hitBall->m_d.m_pos = Vertex3Ds(x, y, z);
      live.hitBall->m_d.m_vel = Vertex3Ds(vx, vy, vz);
      live.hitBall->m_orientation = QuatToMatrix(qw, qx, qy, qz);
      live.hitBall->OnPhysicStepProcessed((uint64_t)t * 1000); // keep the trail/spin history coherent
   }

   // Flipper angles: hold the last recorded angle (records are 1 ms dense while moving)
   const vector<HitFlipper *> &flippers = m_player->m_physics->GetFlippers();
   for (const auto &entry : m_data.GetFlippers())
   {
      const uint8_t id = entry.first;
      if (id >= flippers.size())
         continue;
      const auto &s = entry.second;
      if (s.empty())
         continue;
      size_t &cursor = m_flipperCursors[id];
      while (cursor + 1 < s.size() && s[cursor + 1].t <= t)
         cursor++;
      if (s[cursor].t <= t)
         flippers[id]->m_flipperMover.m_angleCur = s[cursor].angle;
   }
}

namespace
{
   constexpr ImU32 COL_INK_SOFT = IM_COL32(17, 17, 18, 200);
   constexpr ImU32 COL_MINT = IM_COL32(150, 222, 206, 255);  // brand mint

   // 5x7 pixel "R", one bit per pixel, MSB = leftmost column
   void DrawPixelR(ImDrawList *dl, const ImVec2 &origin, float px, bool shadow)
   {
      static constexpr uint8_t R_ROWS[7] = { 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 };
      for (int row = 0; row < 7; row++)
         for (int col = 0; col < 5; col++)
            if (R_ROWS[row] & (0b10000 >> col))
            {
               const ImVec2 p0 { origin.x + col * px, origin.y + row * px };
               const ImVec2 p1 { p0.x + px, p0.y + px };
               if (shadow)
                  dl->AddRectFilled({ p0.x + px * 0.25f, p0.y + px * 0.25f }, { p1.x + px * 0.25f, p1.y + px * 0.25f }, COL_INK_SOFT);
               dl->AddRectFilled(p0, p1, COL_MINT);
            }
   }
}

void ReplayPlayer::DrawOverlays(float scale)
{
   // Exports render CLEAN: the app previews and burns its own overlays (hook
   // text, logo, site tag), so the engine ships bare gameplay frames. The only
   // engine-drawn mark is the blinking pixel R — the "you are rewatching"
   // indicator — in Watch mode, never in a video.
   if (!m_loaded || m_exportMode)
      return;

   if (((m_lastT / 500) & 1) == 0)
      DrawPixelR(ImGui::GetForegroundDrawList(), { 16.f * scale, 16.f * scale }, 4.f * scale, true);
}

Matrix3 ReplayPlayer::QuatToMatrix(float w, float x, float y, float z)
{
   // Inverse of ReplayRecorder::MatrixToQuat: _32-_23 = 4wx, _13-_31 = 4wy, _21-_12 = 4wz
   const float n = sqrtf(w * w + x * x + y * y + z * z);
   if (n > 1e-6f)
   {
      const float inv = 1.f / n;
      w *= inv; x *= inv; y *= inv; z *= inv;
   }
   else
   {
      w = 1.f; x = y = z = 0.f;
   }
   return Matrix3(
      1.f - 2.f * (y * y + z * z), 2.f * (x * y - w * z),       2.f * (x * z + w * y),
      2.f * (x * y + w * z),       1.f - 2.f * (x * x + z * z), 2.f * (y * z - w * x),
      2.f * (x * z - w * y),       2.f * (y * z + w * x),       1.f - 2.f * (x * x + y * y));
}
