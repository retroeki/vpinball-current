// license:GPLv3+

#pragma once

#include <atomic>
#include <cstdint>

// Always-on gameplay counters, independent of the replay recorder and its
// setting: how long a ball was actually in motion this session and how many
// balls were played. Used by the app for score-submission plausibility
// (VPinballGetPlayStats). Physics thread writes, JNI reads — atomics only,
// no locks, no allocation, no disk.
class PlayStats final
{
public:
   // Called once per 1 ms physics cycle. Simulated time, so pause doesn't count.
   void OnPhysicsCycle(bool anyBallMoving)
   {
      m_wallMs.fetch_add(1, std::memory_order_relaxed);
      if (anyBallMoving)
         m_activeMs.fetch_add(1, std::memory_order_relaxed);
   }

   void OnBallDestroyed() { m_ballsDestroyed.fetch_add(1, std::memory_order_relaxed); }

   uint32_t GetWallMs() const { return m_wallMs.load(std::memory_order_relaxed); }
   uint32_t GetActiveMs() const { return m_activeMs.load(std::memory_order_relaxed); }
   uint32_t GetBallsDestroyed() const { return m_ballsDestroyed.load(std::memory_order_relaxed); }

private:
   std::atomic<uint32_t> m_wallMs { 0 };
   std::atomic<uint32_t> m_activeMs { 0 };
   std::atomic<uint32_t> m_ballsDestroyed { 0 };
};
