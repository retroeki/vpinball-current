// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// Parsed in-memory form of a .vpr recording (format v0), shared by the ghost
// debug overlay and the replay player. Samples are sorted by time per track.
class ReplayData final
{
public:
   struct BallSample
   {
      uint32_t t;
      float x, y, z;
      float qw, qx, qy, qz;
   };
   struct BallTrack
   {
      float radius = 25.f;
      float mass = 1.f;
      std::vector<BallSample> samples;
   };
   struct FlipperSample
   {
      uint32_t t;
      float angle;
   };
   struct StateEvent
   {
      uint32_t t;
      uint16_t slot;
      float value;
   };
   struct StateSnapshot
   {
      uint32_t t;
      std::vector<float> values; // one per registry slot
   };

   bool Load(const std::string &path);

   uint32_t GetFirstMs() const { return m_firstMs; }
   uint32_t GetLastMs() const { return m_lastMs; }
   uint32_t GetDurationMs() const { return m_lastMs - m_firstMs; }
   const std::map<uint8_t, BallTrack> &GetBalls() const { return m_balls; }
   const std::map<uint8_t, std::vector<FlipperSample>> &GetFlippers() const { return m_flippers; }
   const std::vector<StateEvent> &GetStateEvents() const { return m_stateEvents; }
   const std::vector<StateSnapshot> &GetStateSnapshots() const { return m_stateSnapshots; }
   size_t GetStateFieldCount() const { return m_stateSnapshots.empty() ? 0 : m_stateSnapshots.front().values.size(); }

   // A gap larger than this between consecutive samples of one ball id means the
   // ball was destroyed and later re-created with the same id: consumers must not
   // interpolate across it, and should treat the ball as absent inside the gap.
   static constexpr uint32_t INTERP_GAP_MS = 50;

private:
   bool ParseChunk(const uint8_t *p, size_t size);

   uint32_t m_firstMs = 0;
   uint32_t m_lastMs = 0;
   std::map<uint8_t, BallTrack> m_balls;
   std::map<uint8_t, std::vector<FlipperSample>> m_flippers;
   std::vector<StateEvent> m_stateEvents;       // chronological
   std::vector<StateSnapshot> m_stateSnapshots; // one per keyframe, chronological
};
