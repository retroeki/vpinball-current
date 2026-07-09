// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>

class Player;
class HitBall;
class HitFlipper;

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

   // Called by Player when balls enter/leave the simulation.
   void OnBallCreated(HitBall *pHitBall);
   void OnBallDestroyed(HitBall *pHitBall);

   // Serialize the current ring content to a .vpr file. Safe to call from any
   // thread; briefly blocks recording while the ring is snapshotted.
   bool SaveToFile(const std::string &path);

   static constexpr uint32_t BALL_SAMPLE_INTERVAL_MS = 8;    // 125 Hz ball transforms
   static constexpr uint32_t RING_WINDOW_MS = 90 * 1000;     // keep the last 90 s
   static constexpr size_t CHUNK_SIZE = 64 * 1024;
   static constexpr uint16_t FORMAT_VERSION = 0;

   // Record types (format v0)
   enum RecordType : uint8_t
   {
      REC_KEYFRAME = 0,     // t u32, nBalls u8 { id u8, radius f32, pos 3xf32, quat 4xi16 }, nFlippers u8 { id u8, angle f32 }
      REC_BALL_SAMPLE = 1,  // t u32, nBalls u8 { id u8, pos 3xf32, quat 4xi16 }
      REC_BALL_CREATE = 2,  // t u32, id u8, radius f32, mass f32, pos 3xf32
      REC_BALL_DESTROY = 3, // t u32, id u8
      REC_FLIPPER = 4,      // t u32, id u8, angle f32
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

   uint32_t m_lastBallSampleMs = UINT32_MAX;
};
