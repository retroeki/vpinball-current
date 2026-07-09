// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayRecorder.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <cfloat>
#include <cmath>

ReplayRecorder::ReplayRecorder(Player *player)
   : m_player(player)
{
   BuildReplayStateRegistry(player, m_stateFields);
   m_lastState.assign(m_stateFields.size(), 0.f);
   // Every chunk starts with a keyframe holding a full state snapshot; make sure one always fits
   m_chunkCapacity = std::max(CHUNK_SIZE, (size_t)(m_stateFields.size() * 4 + 16 * 1024));
   m_cur.data.reserve(m_chunkCapacity);
   PLOGI << "ReplayRecorder: enabled (window " << (RING_WINDOW_MS / 1000) << "s, ball sample " << BALL_SAMPLE_INTERVAL_MS << "ms, " << m_stateFields.size() << " state fields)";
}

ReplayRecorder::~ReplayRecorder()
{
   PLOGI << "ReplayRecorder: shutting down, " << m_sealed.size() << " sealed chunks + current";
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

   if (m_curOpen)
      m_cur.lastTimeMs = t;
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

bool ReplayRecorder::SaveToFile(const std::string &path)
{
   std::deque<Chunk> sealed;
   Chunk cur;
   {
      std::lock_guard<std::mutex> lock(m_mutex);
      sealed = m_sealed;
      cur = m_cur;
   }

   std::vector<const Chunk *> chunks;
   for (const Chunk &c : sealed)
      chunks.push_back(&c);
   if (!cur.data.empty())
      chunks.push_back(&cur);
   if (chunks.empty())
   {
      PLOGI << "ReplayRecorder: nothing recorded, not writing " << path;
      return false;
   }

   try
   {
      std::filesystem::create_directories(std::filesystem::path(path).parent_path());
   }
   catch (const std::exception &e)
   {
      PLOGE << "ReplayRecorder: failed to create directory for " << path << ": " << e.what();
      return false;
   }

   std::ofstream out(path, std::ios::binary | std::ios::trunc);
   if (!out.is_open())
   {
      PLOGE << "ReplayRecorder: failed to open " << path;
      return false;
   }

   const uint32_t firstMs = chunks.front()->firstTimeMs;
   const uint32_t lastMs = chunks.back()->lastTimeMs;
   const uint32_t chunkCount = (uint32_t)chunks.size();
   const uint16_t version = FORMAT_VERSION;
   const uint16_t flags = 0; // bit 0 reserved for compression (Phase 1)

   out.write("VPRP", 4);
   out.write(reinterpret_cast<const char *>(&version), 2);
   out.write(reinterpret_cast<const char *>(&flags), 2);
   out.write(reinterpret_cast<const char *>(&firstMs), 4);
   out.write(reinterpret_cast<const char *>(&lastMs), 4);
   out.write(reinterpret_cast<const char *>(&chunkCount), 4);

   size_t total = 20;
   for (const Chunk *c : chunks)
   {
      const uint32_t size = (uint32_t)c->data.size();
      out.write(reinterpret_cast<const char *>(&c->firstTimeMs), 4);
      out.write(reinterpret_cast<const char *>(&size), 4);
      out.write(reinterpret_cast<const char *>(c->data.data()), size);
      total += 8 + size;
   }
   out.close();

   PLOGI << "ReplayRecorder: wrote " << path << " (" << total << " bytes, " << chunkCount << " chunks, " << (lastMs - firstMs) << "ms)";
   return true;
}

void ReplayRecorder::OpenChunk(uint32_t timeMs)
{
   if (m_curOpen && !m_cur.data.empty())
   {
      m_cur.lastTimeMs = timeMs;
      m_sealed.push_back(std::move(m_cur));
      m_cur = Chunk {};
   }
   m_cur.data.reserve(m_chunkCapacity);
   m_cur.firstTimeMs = timeMs;
   m_cur.lastTimeMs = timeMs;
   m_curOpen = true;
   DropExpiredChunks(timeMs);
   WriteKeyframe(timeMs);
}

void ReplayRecorder::EnsureSpace(size_t bytes, uint32_t timeMs)
{
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

void ReplayRecorder::DropExpiredChunks(uint32_t nowMs)
{
   while (!m_sealed.empty() && nowMs - m_sealed.front().lastTimeMs > RING_WINDOW_MS)
      m_sealed.pop_front();
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
