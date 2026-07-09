// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayGhost.h"

#include "imgui/imgui.h"

#include <cmath>

ReplayGhost::ReplayGhost(Player *player, const std::string &path)
   : m_player(player)
{
   m_loaded = m_data.Load(path);
   if (m_loaded)
   {
      size_t nSamples = 0;
      for (const auto &track : m_data.GetBalls())
         nSamples += track.second.samples.size();
      PLOGI << "ReplayGhost: loaded " << path << " (" << m_data.GetBalls().size() << " balls, " << nSamples << " samples, " << m_data.GetDurationMs() << "ms loop)";
   }
   else
   {
      PLOGW << "ReplayGhost: could not load " << path << ", ghost overlay disabled";
   }
}

void ReplayGhost::Draw()
{
   if (!m_loaded)
      return;

   const uint32_t now = m_player->m_time_msec;
   if (!m_anchored)
   {
      m_anchorMs = now;
      m_anchored = true;
   }
   const uint32_t ghostT = m_data.GetFirstMs() + (now - m_anchorMs) % m_data.GetDurationMs();
   if (ghostT < m_lastGhostT) // loop wrapped: rewind cursors
      for (auto &cursor : m_cursors)
         cursor.second = 0;
   m_lastGhostT = ghostT;

   // Same projection as EditorUI::RenderContext::Project (EditorUI.cpp)
   const float rClipWidth = (float)m_player->m_playfieldWnd->GetWidth() * 0.5f;
   const float rClipHeight = (float)m_player->m_playfieldWnd->GetHeight() * 0.5f;
   const Matrix3D mvp = m_player->m_renderer->GetMVP().GetModelViewProj(0);
   auto project = [&](float x, float y, float z, ImVec2 &out) -> bool
   {
      const float xp = mvp._11 * x + mvp._21 * y + mvp._31 * z + mvp._41;
      const float yp = mvp._12 * x + mvp._22 * y + mvp._32 * z + mvp._42;
      const float wp = mvp._14 * x + mvp._24 * y + mvp._34 * z + mvp._44;
      if (wp <= 1e-10f)
         return false;
      const float inv_wp = 1.0f / wp;
      out = ImVec2 { (wp + xp) * rClipWidth * inv_wp, (wp - yp) * rClipHeight * inv_wp };
      return true;
   };

   ImDrawList *const dl = ImGui::GetBackgroundDrawList();
   constexpr ImU32 colFill = IM_COL32(150, 222, 206, 90);   // translucent mint
   constexpr ImU32 colLine = IM_COL32(150, 222, 206, 220);
   constexpr ImU32 colTrail = IM_COL32(150, 222, 206, 140);

   for (const auto &entry : m_data.GetBalls())
   {
      const ReplayData::BallTrack &track = entry.second;
      const auto &s = track.samples;
      if (s.empty())
         continue;
      size_t &cursor = m_cursors[entry.first];

      // Advance cursor so s[cursor] <= ghostT < s[cursor+1]
      while (cursor + 1 < s.size() && s[cursor + 1].t <= ghostT)
         cursor++;
      const ReplayData::BallSample &a = s[cursor];
      if (a.t > ghostT) // this ball does not exist yet at ghostT
         continue;
      float x = a.x, y = a.y, z = a.z;
      if (cursor + 1 < s.size())
      {
         const ReplayData::BallSample &b = s[cursor + 1];
         if (b.t - a.t > ReplayData::INTERP_GAP_MS)
         {
            if (ghostT > a.t + ReplayData::INTERP_GAP_MS) // ball destroyed: hide until re-created
               continue;
         }
         else if (b.t > a.t)
         {
            const float f = (float)(ghostT - a.t) / (float)(b.t - a.t);
            x += (b.x - a.x) * f;
            y += (b.y - a.y) * f;
            z += (b.z - a.z) * f;
         }
      }
      else if (ghostT > a.t + ReplayData::INTERP_GAP_MS) // past the last sample of this ball
         continue;

      ImVec2 center;
      if (!project(x, y, z, center))
         continue;
      ImVec2 rim;
      float radiusPx = 6.f;
      if (project(x + track.radius, y, z, rim))
      {
         const float dx = rim.x - center.x, dy = rim.y - center.y;
         radiusPx = std::max(3.f, sqrtf(dx * dx + dy * dy));
      }
      dl->AddCircleFilled(center, radiusPx, colFill, 20);
      dl->AddCircle(center, radiusPx, colLine, 20, 1.5f);

      // Short trail: samples from the last ~100 ms
      ImVec2 prev = center;
      size_t idx = cursor;
      while (idx > 0 && ghostT - s[idx].t < 100 && s[idx - 1].t + ReplayData::INTERP_GAP_MS >= s[idx].t)
      {
         idx--;
         ImVec2 pt;
         if (!project(s[idx].x, s[idx].y, s[idx].z, pt))
            break;
         dl->AddLine(prev, pt, colTrail, 1.5f);
         prev = pt;
      }
   }
}
