// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayData.h"
#include "ReplayRecorder.h"

#include <fstream>
#include <cstring>

namespace
{
   uint8_t GetU8(const uint8_t *&p) { return *p++; }
   uint32_t GetU32(const uint8_t *&p)
   {
      uint32_t v;
      memcpy(&v, p, 4);
      p += 4;
      return v;
   }
   float GetF32(const uint8_t *&p)
   {
      float v;
      memcpy(&v, p, 4);
      p += 4;
      return v;
   }
   uint64_t GetU64(const uint8_t *&p)
   {
      uint64_t v;
      memcpy(&v, p, 8);
      p += 8;
      return v;
   }
   uint16_t GetU16(const uint8_t *&p)
   {
      uint16_t v;
      memcpy(&v, p, 2);
      p += 2;
      return v;
   }
   void GetQuat(const uint8_t *&p, float q[4])
   {
      for (int i = 0; i < 4; i++)
      {
         int16_t s;
         memcpy(&s, p, 2);
         p += 2;
         q[i] = (float)s * (1.f / 32767.f);
      }
   }
}

void ReplayData::RleEncode(const uint8_t *src, size_t len, std::vector<uint8_t> &out)
{
   out.clear();
   size_t i = 0;
   while (i < len)
   {
      uint8_t run = 1;
      while (run < 255 && i + run < len && src[i + run] == src[i])
         run++;
      out.push_back(run);
      out.push_back(src[i]);
      i += run;
   }
}

bool ReplayData::RleDecode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstLen)
{
   size_t o = 0;
   for (size_t i = 0; i + 1 < len; i += 2)
   {
      const uint8_t run = src[i];
      if (o + run > dstLen)
         return false;
      memset(dst + o, src[i + 1], run);
      o += run;
   }
   return o == dstLen;
}

bool ReplayData::Load(const std::string &path)
{
   std::ifstream in(path, std::ios::binary);
   if (!in.is_open())
      return false;
   std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
   in.close();

   if (file.size() < 20 || memcmp(file.data(), "VPRP", 4) != 0)
      return false;
   const uint8_t *p = file.data() + 4;
   uint16_t version;
   memcpy(&version, p, 2);
   if (version != ReplayRecorder::FORMAT_VERSION)
   {
      PLOGW << "ReplayData: unsupported format version " << version;
      return false;
   }
   p += 4; // version + flags
   m_firstMs = GetU32(p);
   m_lastMs = GetU32(p);
   const uint32_t chunkCount = GetU32(p);

   const uint8_t *const end = file.data() + file.size();
   for (uint32_t i = 0; i < chunkCount; i++)
   {
      if (p + 8 > end)
         return false;
      GetU32(p); // chunk firstTimeMs, unused: whole file is loaded
      const uint32_t size = GetU32(p);
      if (p + size > end)
         return false;
      // Chunks are self-contained: a malformed one loses its own records
      // only. The old all-or-nothing reject meant one bad record anywhere
      // silently killed playback of the whole clip (Watch fell back to a
      // live game) — see the aux-display key mixup fixed in the recorder.
      if (!ParseChunk(p, size))
      {
         m_skippedRecords++;
         PLOGW << "ReplayData: malformed chunk " << i << " ignored";
      }
      p += size;
   }

   if (m_skippedRecords)
      PLOGW << "ReplayData: " << m_skippedRecords << " inconsistent record(s)/chunk(s) skipped";
   return !m_balls.empty() && m_lastMs > m_firstMs;
}

bool ReplayData::ParseChunk(const uint8_t *p, size_t size)
{
   const uint8_t *const end = p + size;
   while (p < end)
   {
      const uint8_t type = GetU8(p);
      switch (type)
      {
      case ReplayRecorder::REC_KEYFRAME:
      {
         if (p + 5 > end) return false;
         const uint32_t t = GetU32(p);
         const uint8_t nBalls = GetU8(p);
         if (p + nBalls * 25 > end) return false;
         for (uint8_t i = 0; i < nBalls; i++)
         {
            const uint8_t id = GetU8(p);
            BallTrack &track = m_balls[id];
            track.radius = GetF32(p);
            const float x = GetF32(p), y = GetF32(p), z = GetF32(p);
            float q[4];
            GetQuat(p, q);
            if (track.samples.empty() || track.samples.back().t < t)
               track.samples.push_back({ t, x, y, z, q[0], q[1], q[2], q[3] });
         }
         if (p + 1 > end) return false;
         const uint8_t nFlippers = GetU8(p);
         if (p + nFlippers * 5 > end) return false;
         for (uint8_t i = 0; i < nFlippers; i++)
         {
            const uint8_t id = GetU8(p);
            const float angle = GetF32(p);
            auto &samples = m_flippers[id];
            if (samples.empty() || samples.back().t < t)
               samples.push_back({ t, angle });
         }
         if (p + 2 > end) return false;
         uint16_t nState;
         memcpy(&nState, p, 2);
         p += 2;
         if (p + nState * 4 > end) return false;
         {
            StateSnapshot snapshot;
            snapshot.t = t;
            snapshot.values.resize(nState);
            memcpy(snapshot.values.data(), p, (size_t)nState * 4);
            p += (size_t)nState * 4;
            m_stateSnapshots.push_back(std::move(snapshot));
         }
         break;
      }
      case ReplayRecorder::REC_BALL_SAMPLE:
      {
         if (p + 5 > end) return false;
         const uint32_t t = GetU32(p);
         const uint8_t nBalls = GetU8(p);
         if (p + nBalls * 21 > end) return false;
         for (uint8_t i = 0; i < nBalls; i++)
         {
            const uint8_t id = GetU8(p);
            const float x = GetF32(p), y = GetF32(p), z = GetF32(p);
            float q[4];
            GetQuat(p, q);
            BallTrack &track = m_balls[id];
            if (track.samples.empty() || track.samples.back().t < t)
               track.samples.push_back({ t, x, y, z, q[0], q[1], q[2], q[3] });
         }
         break;
      }
      case ReplayRecorder::REC_BALL_CREATE:
      {
         if (p + 5 + 8 + 12 > end) return false;
         const uint32_t t = GetU32(p);
         const uint8_t id = GetU8(p);
         BallTrack &track = m_balls[id];
         track.radius = GetF32(p);
         track.mass = GetF32(p);
         const float x = GetF32(p), y = GetF32(p), z = GetF32(p);
         if (track.samples.empty() || track.samples.back().t < t)
            track.samples.push_back({ t, x, y, z, 1.f, 0.f, 0.f, 0.f });
         break;
      }
      case ReplayRecorder::REC_BALL_DESTROY:
         if (p + 5 > end) return false;
         p += 5;
         break;
      case ReplayRecorder::REC_FLIPPER:
      {
         if (p + 9 > end) return false;
         const uint32_t t = GetU32(p);
         const uint8_t id = GetU8(p);
         const float angle = GetF32(p);
         auto &samples = m_flippers[id];
         if (samples.empty() || samples.back().t < t)
            samples.push_back({ t, angle });
         else if (samples.back().t == t)
            samples.back().angle = angle;
         break;
      }
      case ReplayRecorder::REC_STATE:
      {
         if (p + 10 > end) return false;
         const uint32_t t = GetU32(p);
         uint16_t slot;
         memcpy(&slot, p, 2);
         p += 2;
         const float value = GetF32(p);
         m_stateEvents.push_back({ t, slot, value });
         break;
      }
      case ReplayRecorder::REC_SOUND:
      {
         if (p + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 1 + 1 > end) return false;
         SoundEvent e;
         e.t = GetU32(p);
         e.volumeOffset = GetF32(p);
         e.randomPitch = GetF32(p);
         e.pitch = (int32_t)GetU32(p);
         e.panOffset = GetF32(p);
         e.fadeOffset = GetF32(p);
         e.loopcount = (int32_t)GetU32(p);
         const uint8_t flags = GetU8(p);
         e.useSame = (flags & 1) != 0;
         e.restart = (flags & 2) != 0;
         const uint8_t nameLen = GetU8(p);
         if (p + nameLen > end) return false;
         e.name.assign(reinterpret_cast<const char *>(p), nameLen);
         p += nameLen;
         m_soundEvents.push_back(std::move(e));
         break;
      }
      case ReplayRecorder::REC_SOUND_STOP:
      {
         if (p + 5 > end) return false;
         SoundStopEvent e;
         e.t = GetU32(p);
         const uint8_t nameLen = GetU8(p);
         if (p + nameLen > end) return false;
         e.name.assign(reinterpret_cast<const char *>(p), nameLen);
         p += nameLen;
         m_soundStops.push_back(std::move(e));
         break;
      }
      case ReplayRecorder::REC_ROM_AUDIO:
      {
         if (p + 4 + 1 + 2 + 4 + 2 > end) return false;
         AudioBlock b;
         b.t = GetU32(p);
         b.key = GetU8(p);
         memcpy(&b.rate, p, 2);
         p += 2;
         b.volume = GetF32(p);
         uint16_t count;
         memcpy(&count, p, 2);
         p += 2;
         if (p + (size_t)count * 2 > end) return false;
         b.samples.resize(count);
         memcpy(b.samples.data(), p, (size_t)count * 2);
         p += (size_t)count * 2;
         m_audioBlocks.push_back(std::move(b));
         break;
      }
      case ReplayRecorder::REC_DISPLAY_DEF:
      {
         if (p + 4 + 1 + 8 + 8 + 8 + 2 + 2 + 4 + 1 > end) return false;
         GetU32(p); // t, defs are time-independent
         const uint8_t key = GetU8(p);
         DisplayDef def;
         def.id = GetU64(p);
         def.groupId = GetU64(p);
         def.overrideId = GetU64(p);
         def.width = GetU16(p);
         def.height = GetU16(p);
         def.hardware = GetU32(p);
         def.frameFormat = GetU8(p);
         m_displayDefs.emplace(key, def); // re-emitted per keyframe, first parse wins
         break;
      }
      case ReplayRecorder::REC_SEG_DEF:
      {
         if (p + 4 + 1 + 8 + 8 + 4 + 1 > end) return false;
         GetU32(p);
         const uint8_t key = GetU8(p);
         SegDef def;
         def.id = GetU64(p);
         def.groupId = GetU64(p);
         def.hardware = GetU32(p);
         def.nElements = GetU8(p);
         if (def.nElements > 32 || p + def.nElements > end) return false;
         for (uint8_t i = 0; i < def.nElements; i++)
            def.elementType[i] = GetU8(p);
         m_segDefs.emplace(key, def);
         break;
      }
      case ReplayRecorder::REC_DISPLAY_FRAME:
      {
         if (p + 4 + 1 + 1 + 4 > end) return false;
         DisplayFrameEvent e;
         e.t = GetU32(p);
         e.key = GetU8(p);
         const uint8_t flags = GetU8(p);
         const uint32_t len = GetU32(p);
         if (p + len > end) return false;
         // Record length is known, so an inconsistent frame (no def for its
         // key, or a payload that doesn't match the def's size) is skippable:
         // drop the frame, keep the file
         const auto def = m_displayDefs.find(e.key);
         bool ok = def != m_displayDefs.end();
         if (ok)
         {
            const size_t bpp = def->second.frameFormat == 2 /*SRGB888*/ ? 3 : def->second.frameFormat == 3 /*SRGB565*/ ? 2 : 1;
            const size_t rawLen = (size_t)def->second.width * def->second.height * bpp;
            e.payload.resize(rawLen);
            if (flags & 1)
               ok = RleDecode(p, len, e.payload.data(), rawLen);
            else if (len == rawLen)
               memcpy(e.payload.data(), p, rawLen);
            else
               ok = false;
         }
         p += len;
         if (ok)
            m_displayFrames.push_back(std::move(e));
         else
            m_skippedRecords++;
         break;
      }
      case ReplayRecorder::REC_SEG_FRAME:
      {
         if (p + 4 + 1 + 1 + 2 > end) return false;
         SegFrameEvent e;
         e.t = GetU32(p);
         e.key = GetU8(p);
         const uint8_t flags = GetU8(p);
         const uint16_t len = GetU16(p);
         if (p + len > end) return false;
         // Same skip-don't-reject rule as REC_DISPLAY_FRAME
         const auto def = m_segDefs.find(e.key);
         bool ok = def != m_segDefs.end();
         if (ok)
         {
            const size_t rawLen = (size_t)def->second.nElements * 16;
            e.values.resize(rawLen);
            if (flags & 1)
               ok = RleDecode(p, len, e.values.data(), rawLen);
            else if (len == rawLen)
               memcpy(e.values.data(), p, rawLen);
            else
               ok = false;
         }
         p += len;
         if (ok)
            m_segFrames.push_back(std::move(e));
         else
            m_skippedRecords++;
         break;
      }
      default:
         PLOGW << "ReplayData: unknown record type " << (int)type << ", stopping parse";
         return !m_balls.empty();
      }
   }
   return true;
}
