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
   struct SoundEvent
   {
      uint32_t t;
      float volumeOffset, randomPitch, panOffset, fadeOffset;
      int32_t pitch, loopcount;
      bool useSame, restart;
      std::string name;
   };
   struct SoundStopEvent
   {
      uint32_t t;
      std::string name;
   };
   struct AudioBlock
   {
      uint32_t t;
      uint8_t key;
      uint16_t rate;
      float volume;
      std::vector<int16_t> samples; // mono s16
   };
   struct DisplayDef
   {
      uint64_t id = 0;        // recorded CtlResId
      uint64_t groupId = 0;
      uint64_t overrideId = 0;
      uint16_t width = 0, height = 0;
      uint32_t hardware = 0;
      uint8_t frameFormat = 0; // CTLPI_DISPLAY_FORMAT_xxx; LUM32F frames are stored quantized to 8 bit
   };
   struct SegDef
   {
      uint64_t id = 0;
      uint64_t groupId = 0;
      uint32_t hardware = 0;
      uint8_t nElements = 0;
      uint8_t elementType[32] = {}; // SegElementType
   };
   struct DisplayFrameEvent
   {
      uint32_t t;
      uint8_t key;
      std::vector<uint8_t> payload; // decoded (LUM8 / SRGB888 / SRGB565 per def)
   };
   struct SegFrameEvent
   {
      uint32_t t;
      uint8_t key;
      std::vector<uint8_t> values; // nElements*16 quantized luminances
   };

   // Byte-level run-length codec shared by recorder (encode) and parser (decode)
   static void RleEncode(const uint8_t *src, size_t len, std::vector<uint8_t> &out);
   static bool RleDecode(const uint8_t *src, size_t len, uint8_t *dst, size_t dstLen);

   bool Load(const std::string &path);

   uint32_t GetFirstMs() const { return m_firstMs; }
   uint32_t GetLastMs() const { return m_lastMs; }
   uint32_t GetDurationMs() const { return m_lastMs - m_firstMs; }
   const std::map<uint8_t, BallTrack> &GetBalls() const { return m_balls; }
   const std::map<uint8_t, std::vector<FlipperSample>> &GetFlippers() const { return m_flippers; }
   const std::vector<StateEvent> &GetStateEvents() const { return m_stateEvents; }
   const std::vector<StateSnapshot> &GetStateSnapshots() const { return m_stateSnapshots; }
   size_t GetStateFieldCount() const { return m_stateSnapshots.empty() ? 0 : m_stateSnapshots.front().values.size(); }
   const std::vector<SoundEvent> &GetSoundEvents() const { return m_soundEvents; }
   const std::vector<SoundStopEvent> &GetSoundStops() const { return m_soundStops; }
   const std::vector<AudioBlock> &GetAudioBlocks() const { return m_audioBlocks; }
   const std::map<uint8_t, DisplayDef> &GetDisplayDefs() const { return m_displayDefs; }
   const std::map<uint8_t, SegDef> &GetSegDefs() const { return m_segDefs; }
   const std::vector<DisplayFrameEvent> &GetDisplayFrames() const { return m_displayFrames; }
   const std::vector<SegFrameEvent> &GetSegFrames() const { return m_segFrames; }

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
   std::vector<SoundEvent> m_soundEvents;       // chronological
   std::vector<SoundStopEvent> m_soundStops;    // chronological
   std::vector<AudioBlock> m_audioBlocks;       // chronological
   std::map<uint8_t, DisplayDef> m_displayDefs; // defs are re-emitted per keyframe; first parse wins
   std::map<uint8_t, SegDef> m_segDefs;
   std::vector<DisplayFrameEvent> m_displayFrames; // chronological, RLE pre-decoded
   std::vector<SegFrameEvent> m_segFrames;         // chronological, RLE pre-decoded
};
