// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayPlayer.h"

#include "imgui/imgui.h"

#include <cmath>

ReplayPlayer::ReplayPlayer(Player *player, const std::string &path)
   : m_player(player)
{
   m_loaded = m_data.Load(path);
   if (m_loaded)
   {
      BuildReplayStateRegistry(player, m_stateFields);
      if (m_stateFields.size() != m_data.GetStateFieldCount())
         PLOGW << "ReplayPlayer: state registry mismatch (table has " << m_stateFields.size() << " fields, recording has " << m_data.GetStateFieldCount()
               << "), was the table modified since recording? Applying the overlap only";
      PLOGI << "ReplayPlayer: playback of " << path << " (" << m_data.GetBalls().size() << " balls, " << m_data.GetFlippers().size() << " flippers, "
            << m_data.GetStateEvents().size() << " state events, " << m_data.GetDurationMs() << "ms loop)";
   }
   else
   {
      PLOGW << "ReplayPlayer: could not load " << path << ", nothing will be played back";
   }
}

ReplayPlayer::~ReplayPlayer()
{
   // Balls created through Player::CreateBall are owned and cleaned up by the Player teardown
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

   const uint64_t nowUs = usec();
   if (!m_started)
   {
      m_startUs = nowUs;
      m_started = true;
   }
   const uint32_t t = m_data.GetFirstMs() + (uint32_t)(((nowUs - m_startUs) / 1000) % m_data.GetDurationMs());
   if (t < m_lastT)
      ResetLoop();
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

void ReplayPlayer::DrawIndicator(float scale)
{
   if (!m_loaded)
      return;
   if ((m_lastT / 500) & 1) // 1 Hz blink on the playback clock
      return;

   // 5x7 pixel "R", one bit per pixel, MSB = leftmost column
   static constexpr uint8_t R_ROWS[7] = { 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 };
   const float px = 4.f * scale;             // pixel cell size
   const ImVec2 origin { 16.f * scale, 16.f * scale };
   constexpr ImU32 colShadow = IM_COL32(17, 17, 18, 200); // brand ink
   constexpr ImU32 colPixel = IM_COL32(150, 222, 206, 255); // brand mint

   ImDrawList *const dl = ImGui::GetForegroundDrawList();
   for (int row = 0; row < 7; row++)
      for (int col = 0; col < 5; col++)
         if (R_ROWS[row] & (0b10000 >> col))
         {
            const ImVec2 p0 { origin.x + col * px, origin.y + row * px };
            const ImVec2 p1 { p0.x + px, p0.y + px };
            dl->AddRectFilled({ p0.x + px * 0.25f, p0.y + px * 0.25f }, { p1.x + px * 0.25f, p1.y + px * 0.25f }, colShadow);
            dl->AddRectFilled(p0, p1, colPixel);
         }
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
