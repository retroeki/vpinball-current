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
      if (!ParseChunk(p, size))
         return false;
      p += size;
   }

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
      default:
         PLOGW << "ReplayData: unknown record type " << (int)type << ", stopping parse";
         return !m_balls.empty();
      }
   }
   return true;
}
