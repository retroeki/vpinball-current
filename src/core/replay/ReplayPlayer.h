// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <map>

#include "ReplayData.h"
#include "ReplayState.h"

class Player;
class HitBall;

// Replay playback driver (Phase 1 skeleton): loads a .vpr recording and drives
// the real renderer with it. The table is loaded as scenery only: the script,
// PinMAME and physics stepping are all disabled by Player when this exists
// (Standalone/ReplayPlayback). Each frame this creates/destroys actual render
// balls and writes interpolated transforms and flipper angles directly into
// the objects the renderer reads. Loops the recording endlessly.
class ReplayPlayer final
{
public:
   ReplayPlayer(Player *player, const std::string &path);
   ~ReplayPlayer();

   bool IsLoaded() const { return m_loaded; }

   // Called once per frame from Player::UpdateGameLogic instead of the physics update.
   void Update();

   // Flashing pixel-style "R" indicator, top-left. Called from LiveUI::RenderUI
   // inside an active ImGui frame; scale is the LiveUI overlay scale.
   void DrawIndicator(float scale);

private:
   void ResetLoop();
   void ApplyStateSnapshot(const ReplayData::StateSnapshot &snapshot);
   static Matrix3 QuatToMatrix(float w, float x, float y, float z);

   Player *const m_player;
   ReplayData m_data;
   bool m_loaded = false;

   uint64_t m_startUs = 0;
   bool m_started = false;
   uint32_t m_lastT = 0;

   struct LiveBall
   {
      HitBall *hitBall = nullptr;
      size_t cursor = 0;
      uint32_t lastSampleT = 0; // recording time of the previous frame's position, for velocity estimation
   };
   std::map<uint8_t, LiveBall> m_liveBalls;
   std::map<uint8_t, size_t> m_flipperCursors;

   std::vector<ReplayStateField> m_stateFields;
   size_t m_stateCursor = 0;
   bool m_stateSnapshotApplied = false;
};
