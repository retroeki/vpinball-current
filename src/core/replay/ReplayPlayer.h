// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <unordered_map>

#include "ReplayData.h"
#include "ReplayState.h"
#include "audio/AudioPlayer.h"
#include "plugins/MsgPlugin.h"
#include "plugins/ControllerPlugin.h"

class Player;
class HitBall;
namespace VPX { class Sound; }

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
   void CloseAudioStreams();
   void SetupDisplayPublishing();
   void TeardownDisplayPublishing();
   static Matrix3 QuatToMatrix(float w, float x, float y, float z);

   // Plugin-bus providers: playback republishes the recorded display/segment
   // sources so every consumer (ScoreView, DMD views, external DMD mirroring)
   // renders them exactly as if the controller were live.
   static void OnGetDisplaySrc(const unsigned int msgId, void *userData, void *msgData);
   static void OnGetSegSrc(const unsigned int msgId, void *userData, void *msgData);
   static DisplayFrame MSGPIAPI GetRenderFrameCb(const CtlResId id);
   static SegDisplayFrame MSGPIAPI GetSegStateCb(const CtlResId id);

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

   std::unordered_map<std::string, VPX::Sound *> m_soundsByName;
   size_t m_soundCursor = 0;
   size_t m_soundStopCursor = 0;
   size_t m_audioCursor = 0;
   std::map<uint8_t, VPX::AudioPlayer::AudioStreamID> m_audioStreams;

   struct LiveDisplay
   {
      ReplayData::DisplayDef def;
      std::vector<float> lumBuf;    // served when frameFormat is LUM32F
      std::vector<uint8_t> byteBuf; // served for SRGB888/SRGB565
      unsigned int frameId = 1;
   };
   struct LiveSeg
   {
      ReplayData::SegDef def;
      float values[16 * 32] = {};
      unsigned int frameId = 1;
   };
   std::map<uint8_t, LiveDisplay> m_liveDisplays;
   std::map<uint8_t, LiveSeg> m_liveSegs;
   size_t m_displayFrameCursor = 0;
   size_t m_segFrameCursor = 0;
   unsigned int m_getDisplayId = 0, m_onDisplayChgId = 0, m_getSegId = 0, m_onSegChgId = 0;
   bool m_publishing = false;
   static ReplayPlayer *s_instance;
};
