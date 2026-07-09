// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

class Player;

// Phase 0 debug playback: loads a .vpr recording (normally the previous
// session's replays/last-session.vpr) and draws the recorded ball positions
// as looping ghost markers over live play, via the LiveUI ImGui overlay.
// Purpose: visually validate that recorded transforms and the 8 ms sample
// grid reproduce ball motion faithfully before building the real playback
// mode. Gated by Standalone/ReplayGhost.
class ReplayGhost final
{
public:
   ReplayGhost(Player *player, const std::string &path);

   bool IsLoaded() const { return m_loaded; }

   // Draw ghost markers; must be called inside an active ImGui frame.
   void Draw();

private:
   struct Sample
   {
      uint32_t t;
      float x, y, z;
   };
   struct BallTrack
   {
      float radius = 25.f;
      std::vector<Sample> samples; // sorted by t
      size_t cursor = 0;
   };

   bool Parse(const std::string &path);
   bool ParseChunk(const uint8_t *p, size_t size);

   Player *const m_player;
   bool m_loaded = false;
   uint32_t m_firstMs = 0;
   uint32_t m_lastMs = 0;
   std::map<uint8_t, BallTrack> m_tracks;

   uint32_t m_anchorMs = 0;   // player physics time when playback started
   uint32_t m_lastGhostT = 0; // to detect loop wrap and reset cursors
   bool m_anchored = false;

   // A gap larger than this between consecutive samples means the ball was
   // destroyed and later re-created with the same id: do not interpolate across it.
   static constexpr uint32_t INTERP_GAP_MS = 50;
};
