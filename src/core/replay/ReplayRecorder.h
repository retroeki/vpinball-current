// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "ReplayState.h"

class Player;
class HitBall;
class HitFlipper;
struct AudioUpdateMsg;
namespace VPX { class Sound; }

// Records resulting gameplay state (not inputs) into a rolling in-memory buffer:
// ball transforms on a fixed 8 ms grid, flipper angles per 1 ms physics cycle while
// moving, ball create/destroy events. Replay is playback of this state, never a
// re-simulation (see docs/replay-system-spec.md in the app repo for why).
//
// Master switch: Standalone/ReplayRecord (Settings). When off, Player never
// constructs this object, so the whole system costs nothing.
//
// Phase 0 scope: ball + flipper channels, uncompressed .vpr v0, auto-save on close.
class ReplayRecorder final
{
public:
   explicit ReplayRecorder(Player *player);
   ~ReplayRecorder();

   // Called by PhysicsEngine at the end of every 1 ms physics cycle.
   void OnPhysicsCycle();

   // Called by Player once per rendered frame: diff-record script/physics-driven
   // visual state (lights, primitives, flashers, spinners, gates, targets).
   void OnFrame();

   // Called by AudioPlayer when a table sound effect is played/stopped. The
   // arguments are recorded verbatim; playback re-fires through the same API.
   void OnPlaySound(VPX::Sound *sound, float volumeOffset, float randomPitch, int pitch, float panOffset, float frontRearFadeOffset, int loopcount, bool useSame, bool restart);
   void OnStopSound(VPX::Sound *sound);

   // Called by Player::OnAudioUpdated with controller (PinMAME) PCM: music and
   // callouts. Downmixed to mono s16 at half the source rate for the ring budget.
   void OnRomAudio(const AudioUpdateMsg &msg);

   // Called by Player when balls enter/leave the simulation.
   void OnBallCreated(HitBall *pHitBall);
   void OnBallDestroyed(HitBall *pHitBall);

   // Serialize the current ring content to a .vpr file. Safe to call from any
   // thread; briefly blocks recording while the ring is snapshotted.
   bool SaveToFile(const std::string &path);

   static constexpr uint32_t BALL_SAMPLE_INTERVAL_MS = 8;    // 125 Hz ball transforms
   static constexpr uint32_t RING_WINDOW_MS = 90 * 1000;     // keep the last 90 s
   static constexpr size_t CHUNK_SIZE = 64 * 1024;
   static constexpr uint16_t FORMAT_VERSION = 2;

   // Record types (format v2)
   enum RecordType : uint8_t
   {
      REC_KEYFRAME = 0,     // t u32, nBalls u8 { id u8, radius f32, pos 3xf32, quat 4xi16 }, nFlippers u8 { id u8, angle f32 }, nStateFields u16, f32 x nStateFields (registry order)
      REC_BALL_SAMPLE = 1,  // t u32, nBalls u8 { id u8, pos 3xf32, quat 4xi16 }
      REC_BALL_CREATE = 2,  // t u32, id u8, radius f32, mass f32, pos 3xf32
      REC_BALL_DESTROY = 3, // t u32, id u8
      REC_FLIPPER = 4,      // t u32, id u8, angle f32
      REC_STATE = 5,        // t u32, slot u16, value f32
      REC_SOUND = 6,        // t u32, volumeOffset f32, randomPitch f32, pitch i32, panOffset f32, fadeOffset f32, loopcount i32, flags u8 (bit0 useSame, bit1 restart), nameLen u8, name bytes
      REC_SOUND_STOP = 7,   // t u32, nameLen u8, name bytes
      REC_ROM_AUDIO = 8,    // t u32, streamKey u8, rate u16, volume f32, count u16, s16 x count (mono)
   };

private:
   struct Chunk
   {
      uint32_t firstTimeMs = 0;
      uint32_t lastTimeMs = 0;
      std::vector<uint8_t> data;
   };

   void OpenChunk(uint32_t timeMs);          // seals current chunk, starts a new one with a keyframe
   void EnsureSpace(size_t bytes, uint32_t timeMs);
   void WriteKeyframe(uint32_t timeMs);
   void DropExpiredChunks(uint32_t nowMs);
   uint8_t BallId(HitBall *pHitBall);

   // append helpers into m_cur.data
   void PutU8(uint8_t v);
   void PutU16(uint16_t v);
   void PutU32(uint32_t v);
   void PutF32(float v);
   void PutQuat(const float q[4]);
   static void MatrixToQuat(const HitBall *pHitBall, float q[4]);

   Player *const m_player;
   std::mutex m_mutex; // guards ring structure vs SaveToFile; appends are game-thread only

   std::deque<Chunk> m_sealed;
   Chunk m_cur;
   bool m_curOpen = false;

   std::unordered_map<HitBall *, uint8_t> m_ballIds;
   uint8_t m_nextBallId = 0;

   std::vector<HitFlipper *> m_flippers;    // bound lazily on first physics cycle
   std::vector<float> m_lastFlipperAngle;
   bool m_flippersBound = false;

   std::vector<ReplayStateField> m_stateFields;
   std::vector<float> m_lastState;          // last recorded value per state slot
   bool m_stateInit = false;
   size_t m_chunkCapacity = CHUNK_SIZE;     // grown when the keyframe alone would not fit

   std::unordered_map<uint32_t, uint8_t> m_audioKeys; // controller audio source id -> small stream key
   std::vector<int16_t> m_audioScratch;               // downmix buffer, reused

   uint32_t m_lastBallSampleMs = UINT32_MAX;
};
