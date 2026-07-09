// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <map>

#include "ReplayData.h"

class Player;

// Phase 0 debug playback: loads a .vpr recording (normally the previous
// session's replays/last-session.vpr) and draws the recorded ball positions
// as looping ghost markers over live play, via the LiveUI ImGui overlay.
// Purpose: visually validate that recorded transforms and the 8 ms sample
// grid reproduce ball motion faithfully. Gated by Standalone/ReplayGhost.
class ReplayGhost final
{
public:
   ReplayGhost(Player *player, const std::string &path);

   bool IsLoaded() const { return m_loaded; }

   // Draw ghost markers; must be called inside an active ImGui frame.
   void Draw();

private:
   Player *const m_player;
   ReplayData m_data;
   bool m_loaded = false;

   std::map<uint8_t, size_t> m_cursors;
   uint32_t m_anchorMs = 0;   // player physics time when playback started
   uint32_t m_lastGhostT = 0; // to detect loop wrap and reset cursors
   bool m_anchored = false;
};
