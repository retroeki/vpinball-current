// license:GPLv3+

#include "core/stdafx.h"

#include "ReplayGhost.h"
#include "ReplayRecorder.h"

#include "imgui/imgui.h"

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
}

ReplayGhost::ReplayGhost(Player *player, const std::string &path)
   : m_player(player)
{
   m_loaded = Parse(path);
   if (m_loaded)
   {
      size_t nSamples = 0;
      for (const auto &track : m_tracks)
         nSamples += track.second.samples.size();
      PLOGI << "ReplayGhost: loaded " << path << " (" << m_tracks.size() << " balls, " << nSamples << " samples, " << (m_lastMs - m_firstMs) << "ms loop)";
   }
   else
   {
      PLOGW << "ReplayGhost: could not load " << path << ", ghost overlay disabled";
   }
}

bool ReplayGhost::Parse(const std::string &path)
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
      PLOGW << "ReplayGhost: unsupported format version " << version;
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
      GetU32(p); // chunk firstTimeMs, unused here
      const uint32_t size = GetU32(p);
      if (p + size > end)
         return false;
      if (!ParseChunk(p, size))
         return false;
      p += size;
   }

   for (auto &track : m_tracks)
      if (track.second.samples.empty())
         return false;
   return !m_tracks.empty() && m_lastMs > m_firstMs;
}

bool ReplayGhost::ParseChunk(const uint8_t *p, size_t size)
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
            BallTrack &track = m_tracks[id];
            track.radius = GetF32(p);
            const float x = GetF32(p), y = GetF32(p), z = GetF32(p);
            p += 8; // quat, unused by the ghost
            if (track.samples.empty() || track.samples.back().t < t)
               track.samples.push_back({ t, x, y, z });
         }
         if (p + 1 > end) return false;
         const uint8_t nFlippers = GetU8(p);
         if (p + nFlippers * 5 > end) return false;
         p += nFlippers * 5; // flipper snapshot, unused by the ghost
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
            p += 8; // quat
            BallTrack &track = m_tracks[id];
            if (track.samples.empty() || track.samples.back().t < t)
               track.samples.push_back({ t, x, y, z });
         }
         break;
      }
      case ReplayRecorder::REC_BALL_CREATE:
      {
         if (p + 5 + 8 + 12 > end) return false;
         const uint32_t t = GetU32(p);
         const uint8_t id = GetU8(p);
         BallTrack &track = m_tracks[id];
         track.radius = GetF32(p);
         GetF32(p); // mass, unused
         const float x = GetF32(p), y = GetF32(p), z = GetF32(p);
         if (track.samples.empty() || track.samples.back().t < t)
            track.samples.push_back({ t, x, y, z });
         break;
      }
      case ReplayRecorder::REC_BALL_DESTROY:
         if (p + 5 > end) return false;
         p += 5;
         break;
      case ReplayRecorder::REC_FLIPPER:
         if (p + 9 > end) return false;
         p += 9;
         break;
      default:
         PLOGW << "ReplayGhost: unknown record type " << (int)type << ", stopping parse";
         return !m_tracks.empty();
      }
   }
   return true;
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
   const uint32_t duration = m_lastMs - m_firstMs;
   const uint32_t ghostT = m_firstMs + (now - m_anchorMs) % duration;
   if (ghostT < m_lastGhostT) // loop wrapped: rewind cursors
      for (auto &track : m_tracks)
         track.second.cursor = 0;
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

   for (auto &entry : m_tracks)
   {
      BallTrack &track = entry.second;
      const std::vector<Sample> &s = track.samples;

      // Advance cursor so s[cursor] <= ghostT < s[cursor+1]
      while (track.cursor + 1 < s.size() && s[track.cursor + 1].t <= ghostT)
         track.cursor++;
      const Sample &a = s[track.cursor];
      if (a.t > ghostT) // this ball does not exist yet at ghostT
         continue;
      float x = a.x, y = a.y, z = a.z;
      if (track.cursor + 1 < s.size())
      {
         const Sample &b = s[track.cursor + 1];
         if (b.t - a.t > INTERP_GAP_MS)
         {
            if (ghostT > a.t + INTERP_GAP_MS) // ball destroyed: hide until re-created
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
      else if (ghostT > a.t + INTERP_GAP_MS) // past the last sample of this ball
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
      size_t idx = track.cursor;
      while (idx > 0 && ghostT - s[idx].t < 100 && s[idx - 1].t + INTERP_GAP_MS >= s[idx].t)
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
