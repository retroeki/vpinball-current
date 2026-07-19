// license:GPLv3+

#include <cassert>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "plugins/MsgPlugin.h"
#include "plugins/VPXPlugin.h"
#include "plugins/ControllerPlugin.h"
#include "common.h"
#include "serum-decode.h"

#include <filesystem>

#include "plugins/LoggingPlugin.h"

using namespace std::string_literals;

namespace Serum {

LPI_IMPLEMENT

///////////////////////////////////////////////////////////////////////////////
// Serum Colorization plugin
//
// This plugin only rely on the generic message plugin API, the generic controller
// plugin API and the following messages:
// - PinMame/OnGameStart: msgData is PinMame game identifier (rom name)
// - PinMame/OnGameEnd

static const MsgPluginAPI* msgApi = nullptr;
static VPXPluginAPI* vpxApi = nullptr;

static uint32_t endpointId;
static unsigned int onControllerGameStartId, onControllerGameEndId;
static unsigned int onDmdSrcChangedId, getDmdSrcId, onDmdTrigger;

static bool isRunning = false;
static std::mutex sourceMutex;
static std::mutex stateMutex;
static std::thread colorizeThread;
static DisplaySrcId dmdId = {};

static Serum_Frame_Struc* pSerum = nullptr;
static unsigned int lastRawFrameId = 0;

MSGPI_STRING_VAL_SETTING(serumPathProp, "SerumPath", "Serum Path", "Folder that cotains Serum colorization files (cRZ, cROMc)", true, "", 1024);

class ColorizationState final
{
public:
   // [serum-16high fix] cropBars: the identify/colorize frames carry 8 (16 at HD) black
   // bar rows above and below the 16 content rows (128x16 hardware packs are authored
   // centered in 128x32). The render sources expose only the content rows - a pointer
   // offset into the full-height buffers - so the on-screen DMD keeps its real 8:1 shape.
   ColorizationState(unsigned int width, unsigned int height, bool cropBars)
      : m_colorFrame(pSerum->SerumVersion == SERUM_V1 ? new uint8_t[width * height * 3] : nullptr)
      , m_width(width), m_height(height)
      , m_cropBars(cropBars)
      , m_colorizedFrameFormat(pSerum->SerumVersion == SERUM_V1 ? CTLPI_DISPLAY_FORMAT_SRGB888 : CTLPI_DISPLAY_FORMAT_SRGB565)
      , m_colorizedframeId(static_cast<unsigned int>(std::rand()))
   {
      assert(m_width > 0);
      assert(m_height > 0);
   }

   ~ColorizationState()
   {
      delete[] m_colorFrame;
      delete[] m_ownFrame32[0];
      delete[] m_ownFrame32[1];
      delete[] m_ownFrame64[0];
      delete[] m_ownFrame64[1];
   }

   void UpdateFrameV1()
   {
      assert(pSerum && (pSerum->SerumVersion == SERUM_V1));
      for (unsigned int i = 0; i < m_width * m_height; i++)
         memcpy(&(m_colorFrame[i * 3]), &pSerum->palette[pSerum->frame[i] * 3], 3);
      m_colorizedframeId++;
   }

   // [serum-v2 fix] V2 render frames are copied out of libserum's working buffers into
   // plugin-owned double buffers: libserum rewrites frame32/frame64 in place during
   // Serum_Colorize/Serum_Rotate while the render thread may be reading the pointer last
   // returned by GetRenderFrame (the long-standing tearing TODO there). Publishing a copy
   // and swapping front buffers keeps the published pointer stable and complete.
   void UpdateFrame32V2()
   {
      assert(pSerum && (pSerum->SerumVersion == SERUM_V2));
      if (pSerum->width32 > 0)
      {
         PublishFrame32(pSerum->frame32, pSerum->width32);
         m_frame32Synthetic = false;
      }
   }

   void UpdateFrame64V2()
   {
      assert(pSerum && (pSerum->SerumVersion == SERUM_V2));
      if (pSerum->width64 > 0)
      {
         PublishFrame64(pSerum->frame64, pSerum->width64);
         m_frame64Synthetic = false;
      }
   }

   // [serum-v2 fix] 'HD' Serum V2 files carry the extra-resolution plane only for a
   // subset of authored frames (isextraframe is per frame). The host displays the
   // highest-resolution render source, so when a matched frame lacks that plane the
   // on-screen DMD held a stale frame while the frameId kept advancing = the reported
   // colour-DMD stutter (BBB: f32=10 f64=0 stretches in [SerumDiag]). When only one
   // plane is updated, derive the other from it so both sources always advance together.
   // Only active once the other plane's source exists (width known) and the standard 2:1
   // Serum plane relationship holds.
   void SynthesizeFrame64From32()
   {
      if (m_width32 == 0 || m_width64 == 0 || m_width64 != m_width32 * 2 || m_colorFrame32 == nullptr)
         return;
      const uint16_t* const src = reinterpret_cast<const uint16_t*>(m_colorFrame32);
      EnsureBuffers64(m_width64);
      uint16_t* const dst = m_ownFrame64[1 - m_front64];
      for (unsigned int y = 0; y < 64; y++)
      {
         const uint16_t* s = src + (y / 2) * m_width32;
         uint16_t* d = dst + y * m_width64;
         for (unsigned int x = 0; x < m_width32; x++)
         {
            d[x * 2] = s[x];
            d[x * 2 + 1] = s[x];
         }
      }
      m_front64 = 1 - m_front64;
      m_colorFrame64 = reinterpret_cast<uint8_t*>(m_ownFrame64[m_front64]);
      m_frame64Synthetic = true;
      m_colorizedframeId++;
   }

   void SynthesizeFrame32From64()
   {
      if (m_width32 == 0 || m_width64 == 0 || m_width64 != m_width32 * 2 || m_colorFrame64 == nullptr)
         return;
      const uint16_t* const src = reinterpret_cast<const uint16_t*>(m_colorFrame64);
      EnsureBuffers32(m_width32);
      uint16_t* const dst = m_ownFrame32[1 - m_front32];
      for (unsigned int y = 0; y < 32; y++)
      {
         const uint16_t* s = src + (y * 2) * m_width64;
         uint16_t* d = dst + y * m_width32;
         for (unsigned int x = 0; x < m_width32; x++)
            d[x] = s[x * 2];
      }
      m_front32 = 1 - m_front32;
      m_colorFrame32 = reinterpret_cast<uint8_t*>(m_ownFrame32[m_front32]);
      m_frame32Synthetic = true;
      m_colorizedframeId++;
   }

private:
   void EnsureBuffers32(unsigned int width)
   {
      if (m_alloc32 != width)
      {
         delete[] m_ownFrame32[0];
         delete[] m_ownFrame32[1];
         m_ownFrame32[0] = new uint16_t[32 * width]();
         m_ownFrame32[1] = new uint16_t[32 * width]();
         m_alloc32 = width;
      }
   }

   void EnsureBuffers64(unsigned int width)
   {
      if (m_alloc64 != width)
      {
         delete[] m_ownFrame64[0];
         delete[] m_ownFrame64[1];
         m_ownFrame64[0] = new uint16_t[64 * width]();
         m_ownFrame64[1] = new uint16_t[64 * width]();
         m_alloc64 = width;
      }
   }

   void PublishFrame32(const uint16_t* src, unsigned int width)
   {
      EnsureBuffers32(width);
      uint16_t* const back = m_ownFrame32[1 - m_front32];
      memcpy(back, src, 32 * width * sizeof(uint16_t));
      m_front32 = 1 - m_front32;
      m_width32 = width;
      m_colorFrame32 = reinterpret_cast<uint8_t*>(m_ownFrame32[m_front32]);
      m_colorizedframeId++;
   }

   void PublishFrame64(const uint16_t* src, unsigned int width)
   {
      EnsureBuffers64(width);
      uint16_t* const back = m_ownFrame64[1 - m_front64];
      memcpy(back, src, 64 * width * sizeof(uint16_t));
      m_front64 = 1 - m_front64;
      m_width64 = width;
      m_colorFrame64 = reinterpret_cast<uint8_t*>(m_ownFrame64[m_front64]);
      m_colorizedframeId++;
   }

   uint16_t* m_ownFrame32[2] = { nullptr, nullptr };
   uint16_t* m_ownFrame64[2] = { nullptr, nullptr };
   unsigned int m_alloc32 = 0, m_alloc64 = 0;
   int m_front32 = 0, m_front64 = 0;

public:
   // Serum v1
   uint8_t* const m_colorFrame;

   // Serum v2 (m_colorFrame32/64 point at the plugin-owned front buffers)
   unsigned int m_width32 = 0;
   uint8_t* m_colorFrame32 = nullptr;
   unsigned int m_width64 = 0;
   uint8_t* m_colorFrame64 = nullptr;
   bool m_frame32Synthetic = false;
   bool m_frame64Synthetic = false;

   // Common state information
   const unsigned int m_width, m_height; // Size of identify frame (which can differ from the size of the colorized frame)
   const bool m_cropBars;                // [serum-16high fix] render sources skip the centering bar rows
   const unsigned int m_colorizedFrameFormat;
   unsigned int m_colorizedframeId = 0;
   bool m_hasAnimation = false;
   std::chrono::high_resolution_clock::time_point m_animationTick;
   std::chrono::high_resolution_clock::time_point m_animationNextTick;
};

static ColorizationState* state = nullptr;

// [SerumDiag] compile-time diagnostic gate (see reference: diagnostic log switches).
// Emits one summary line per second from the colorize thread so colour-DMD stutter
// reports can be diagnosed from logcat. Flip to 0 (or strip) for release builds.
#define SERUM_DIAG 0 // 1 = [SerumDiag] per-second + [SerumFrame] per-frame logcat diagnostics (keep 0 for release; CRCs comparable with tools/dmd-colorization-rig)
#if SERUM_DIAG
static std::atomic<unsigned int> diagRenderCalls{0};
static uint32_t diagCrcTable[256];
static void DiagCrcInit()
{
   for (uint32_t i = 0; i < 256; i++)
   {
      uint32_t crc = i;
      for (int j = 0; j < 8; j++)
         crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
      diagCrcTable[i] = crc;
   }
}
#endif

static void ColorizeThread()
{
   SetThreadName("Serum.ColorizeThread"s);
   unsigned int lastFrameId = 0;
#if SERUM_DIAG
   DiagCrcInit();
   auto diagLastLog = std::chrono::high_resolution_clock::now();
   unsigned int dPolls = 0, dIdChg = 0, dNew = 0, dSame = 0, dNoF = 0, dRotBranch = 0, dRotCalls = 0, dF32 = 0, dF64 = 0;
   double dColMsSum = 0.0, dColMsMax = 0.0;
#endif
   while (isRunning)
   {
      // Original PinMAME code would evaluate DMD frames at a fixed 60 FPS and color rotation are also based on a 60FPS rate. So update at this pace.
      std::this_thread::sleep_for(std::chrono::microseconds(16666));

      std::lock_guard<std::mutex> lock1(sourceMutex);
      if (dmdId.id.id == 0)
         continue;

      const DisplayFrame frame = dmdId.GetIdentifyFrame(dmdId.id);
      if (frame.frame == nullptr)
         continue; // transient null (source mid-teardown/startup) must not permanently kill colorization

      // [serum-16high fix] Serum packs for 128x16 hardware (early Data East, e.g.
      // Star Trek 25th trek_201) are authored at 128x32 with the 16 content rows
      // vertically CENTERED (rows 8..23) - that is how the Windows pipeline presents
      // 16-high DMDs to colorizers. Feeding the raw 128x16 frame makes libserum hash
      // fwidth*fheight = 4096 bytes of a 2048-byte buffer: nothing ever identifies and
      // the colorized override shows nothing (user-reported blank DMD). Verified against
      // trek_201.cROMc: centered = 620/744 frames colorized; raw or line-doubled = ~0.
      const uint8_t* identifyFrame = static_cast<const uint8_t*>(frame.frame);
      unsigned int identifyWidth = dmdId.width;
      unsigned int identifyHeight = dmdId.height;
      static std::vector<uint8_t> centered16;
      if (identifyHeight == 16)
      {
         centered16.assign((size_t)identifyWidth * 32, 0);
         memcpy(centered16.data() + (size_t)identifyWidth * 8, identifyFrame, (size_t)identifyWidth * 16);
         identifyFrame = centered16.data();
         identifyHeight = 32;
      }

#if SERUM_DIAG
      dPolls++;
#endif
      if (frame.frameId != lastFrameId)
      {
         // We received a new identify frame to match & colorize
         lastFrameId = frame.frameId;
#if SERUM_DIAG
         dIdChg++;
         const auto diagColT0 = std::chrono::high_resolution_clock::now();
#endif
         const uint32_t firstrot = Serum_Colorize(const_cast<uint8_t*>(identifyFrame));
#if SERUM_DIAG
         const double diagColMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - diagColT0).count();
         dColMsSum += diagColMs;
         if (diagColMs > dColMsMax) dColMsMax = diagColMs;
         if (firstrot == IDENTIFY_NO_FRAME) dNoF++;
         else if (firstrot == IDENTIFY_SAME_FRAME) dSame++;
         else
         {
            dNew++;
            if (pSerum->flags & FLAG_RETURNED_32P_FRAME_OK) dF32++;
            if (pSerum->flags & FLAG_RETURNED_64P_FRAME_OK) dF64++;
         }
         // [SerumDiag] per-frame signature: bitmask of occurring pixel values (0-15) +
         // plain CRC32 (zlib-compatible; identical to libserum crc32_fast with no
         // mask/shape) for offline membership tests against the file's hashcode table
         {
            const uint8_t* const f8 = identifyFrame;
            const unsigned int nPix = identifyWidth * identifyHeight;
            unsigned int valMask = 0;
            uint32_t crc = 0xffffffff;
            for (unsigned int i = 0; i < nPix; i++)
            {
               valMask |= 1u << (f8[i] & 15);
               crc = (crc >> 8) ^ diagCrcTable[(f8[i] ^ crc) & 0xFF];
            }
            crc = ~crc;
            LOGI("[SerumFrame] res=%s vals=%04x crc=%08x",
               firstrot == IDENTIFY_NO_FRAME ? "nof" : (firstrot == IDENTIFY_SAME_FRAME ? "same" : "new"), valMask, crc);
         }
#endif
         // IDENTIFY_SAME_FRAME (0xFFFFFFFE) = Serum matched the SAME frame as last time and produced
         // no new colorized output; treat it like "no new frame". Without excluding it here, the block
         // below sets m_hasAnimation = (0xFFFFFFFE != 0) = true and m_animationNextTick = now +
         // milliseconds(0xFFFFFFFE) ~= 49 days, which permanently freezes Serum color-rotation
         // (palette-cycling) effects. Skipping the block leaves the existing animation ticking.
         if (firstrot != IDENTIFY_NO_FRAME && firstrot != IDENTIFY_SAME_FRAME)
         {
            // New frame, eventually starting a new animation
            std::lock_guard<std::mutex> lock2(stateMutex);
            bool newState = false;
            // [serum-16high fix] state dimensions follow the (possibly centered/padded)
            // frame actually fed to Serum, not the raw display size: pack frames are
            // fwidth x fheight (128x32 for 16-high hardware)
            const bool cropBars = identifyFrame != static_cast<const uint8_t*>(frame.frame); // 16-high frame was center-padded
            if (state == nullptr)
            {
               state = new ColorizationState(identifyWidth, identifyHeight, cropBars);
               newState = true;
            }
            else if (state->m_width != identifyWidth || state->m_height != identifyHeight)
            {
               delete state;
               state = new ColorizationState(identifyWidth, identifyHeight, cropBars);
               newState = true;
            }
            
            // [serum-scene fix] Serum_Colorize returns the delay until the next
            // rotation/scene step in the LOW WORD only; the high bits are
            // FLAG_RETURNED_V2_* markers (FLAG_RETURNED_V2_SCENE = 0x40000 when the
            // matched frame started/continues an authored scene). Using the raw value
            // scheduled the next Serum_Rotate() 262+ seconds out, so scene-driven
            // packs (e.g. GTS3 SMB: 682 trigger frames + scene generator) never
            // played their scenes and the colorized DMD froze between rare matches.
            const uint32_t animDelayMs = firstrot & 0x0000ffff;
            state->m_hasAnimation = animDelayMs != 0;
            if (state->m_hasAnimation)
            {
               state->m_animationTick = std::chrono::high_resolution_clock::now();
               state->m_animationNextTick = state->m_animationTick + std::chrono::milliseconds(animDelayMs);
            }
            if (pSerum->SerumVersion == SERUM_V1)
               state->UpdateFrameV1();
            else if (pSerum->SerumVersion == SERUM_V2)
            {
               const bool ok32 = (pSerum->flags & FLAG_RETURNED_32P_FRAME_OK) != 0;
               const bool ok64 = (pSerum->flags & FLAG_RETURNED_64P_FRAME_OK) != 0;
               if (ok32)
                  state->UpdateFrame32V2();
               if (ok64)
                  state->UpdateFrame64V2();
               // [serum-v2 fix] keep the plane this frame didn't author in step
               if (ok32 && !ok64)
                  state->SynthesizeFrame64From32();
               else if (ok64 && !ok32)
                  state->SynthesizeFrame32From64();
            }
            
            // This supposes that we won't decode another frame with a pup trigger before the message will be processed on main thread (should be ok)
            if (pSerum->triggerID != 0xffffffff)
               msgApi->RunOnMainThread(0, [](void* userData) { msgApi->BroadcastMsg(endpointId, onDmdTrigger, &pSerum->triggerID); }, nullptr);

            if (newState)
               msgApi->RunOnMainThread(0, [](void* userData) { msgApi->BroadcastMsg(endpointId, onDmdSrcChangedId, nullptr); }, nullptr);
         }
      }
      else if (state && state->m_hasAnimation)
      {
#if SERUM_DIAG
         dRotBranch++;
#endif
         // [serum-v2 fix] frame publication must be synchronized against GetRenderFrame;
         // the new-frame branch already locks stateMutex, this branch previously did not
         std::lock_guard<std::mutex> lock2(stateMutex);
         // Perform current animation (catching up to the current time point)
         const auto now = std::chrono::high_resolution_clock::now();
         while (state->m_animationNextTick < now)
         {
            const uint32_t nextrot = Serum_Rotate();
#if SERUM_DIAG
            dRotCalls++;
#endif
            const uint32_t delayMs = nextrot & 0x0000ffff;
            if (delayMs == 0)
            {
               state->m_hasAnimation = false;
               break;
            }
            state->m_animationTick = state->m_animationNextTick;
            state->m_animationNextTick = state->m_animationNextTick + std::chrono::milliseconds(delayMs);
            if ((pSerum->SerumVersion == SERUM_V1) && (nextrot & FLAG_RETURNED_V1_ROTATED))
               state->UpdateFrameV1();
            if (pSerum->SerumVersion == SERUM_V2)
            {
               // [serum-scene fix] Serum_Rotate() also advances authored scene playback
               // (Serum_ApplyRotationsv2 calls Serum_RenderScene internally). A rendered
               // scene frame is flagged with FLAG_RETURNED_V2_SCENE and fills the planes
               // reported in pSerum->flags, without the ROTATED32/64 bits.
               const bool scene = (nextrot & FLAG_RETURNED_V2_SCENE) != 0;
               const bool rot32 = (nextrot & FLAG_RETURNED_V2_ROTATED32) != 0
                               || (scene && (pSerum->flags & FLAG_RETURNED_32P_FRAME_OK) != 0);
               const bool rot64 = (nextrot & FLAG_RETURNED_V2_ROTATED64) != 0
                               || (scene && (pSerum->flags & FLAG_RETURNED_64P_FRAME_OK) != 0);
               if (rot32)
                  state->UpdateFrame32V2();
               if (rot64)
                  state->UpdateFrame64V2();
               // [serum-v2 fix] a derived plane must follow the authored plane's rotation
               if (rot32 && !rot64 && state->m_frame64Synthetic)
                  state->SynthesizeFrame64From32();
               else if (rot64 && !rot32 && state->m_frame32Synthetic)
                  state->SynthesizeFrame32From64();
            }
         }
      }
#if SERUM_DIAG
      {
         const auto diagNow = std::chrono::high_resolution_clock::now();
         if (std::chrono::duration_cast<std::chrono::milliseconds>(diagNow - diagLastLog).count() >= 1000)
         {
            LOGI("[SerumDiag] polls=%u idChg=%u new=%u same=%u nof=%u colMsAvg=%.2f colMsMax=%.2f rotBranch=%u rotCalls=%u f32=%u f64=%u render=%u anim=%d",
               dPolls, dIdChg, dNew, dSame, dNoF, dIdChg ? (dColMsSum / dIdChg) : 0.0, dColMsMax, dRotBranch, dRotCalls, dF32, dF64,
               diagRenderCalls.exchange(0), (state && state->m_hasAnimation) ? 1 : 0);
            diagLastLog = diagNow;
            dPolls = dIdChg = dNew = dSame = dNoF = dRotBranch = dRotCalls = dF32 = dF64 = 0;
            dColMsSum = dColMsMax = 0.0;
         }
      }
#endif
   }
   isRunning = false;
}

static DisplayFrame GetRenderFrame(const CtlResId id)
{
#if SERUM_DIAG
   diagRenderCalls.fetch_add(1, std::memory_order_relaxed);
#endif
   // TODO To be fully clean we should do a copy of the render (since the direct data is updated asynchronously, so eventually while it is read by consumer)
   std::lock_guard<std::mutex> lock(stateMutex);
   if (state == nullptr)
      return { 0, nullptr };
   // [serum-16high fix] with cropBars the render view skips the black centering rows:
   // 32-row colorized frames expose rows 8..23, 64-row (HD) frames rows 16..47. Rows
   // are contiguous so the cropped view is just an offset into the published buffer.
   else if (id.resId == 1) // Serum V2 Height 64
      return { state->m_colorizedframeId, (state->m_cropBars && state->m_colorFrame64) ? state->m_colorFrame64 + 16 * state->m_width64 * sizeof(uint16_t) : state->m_colorFrame64 };
   else if (state->m_colorFrame32) // Serum V2 Height 32
      return { state->m_colorizedframeId, state->m_cropBars ? state->m_colorFrame32 + 8 * state->m_width32 * sizeof(uint16_t) : state->m_colorFrame32 };
   else // Serum V1
      return { state->m_colorizedframeId, (state->m_cropBars && state->m_colorFrame) ? state->m_colorFrame + 8 * state->m_width * 3 : state->m_colorFrame };
}

static void OnGetRenderDMDSrc(const unsigned int eventId, void* userData, void* msgData)
{
   if (pSerum == nullptr || state == nullptr || dmdId.id.id == 0)
      return;
   GetDisplaySrcMsg& msg = *static_cast<GetDisplaySrcMsg*>(msgData);
   if (state->m_colorFrame32 && state->m_width32)
   {
      if (msg.count < msg.maxEntryCount)
      {
         msg.entries[msg.count] = {};
         msg.entries[msg.count].id = { { endpointId, 0 } };
         msg.entries[msg.count].overrideId = dmdId.id;
         msg.entries[msg.count].width = state->m_width32;
         msg.entries[msg.count].height = state->m_cropBars ? 16 : 32; // [serum-16high fix]
         msg.entries[msg.count].hardware = CTLPI_DISPLAY_HARDWARE_RGB_LED;
         msg.entries[msg.count].frameFormat = CTLPI_DISPLAY_FORMAT_SRGB565;
         msg.entries[msg.count].GetRenderFrame = &GetRenderFrame;
      }
      msg.count++;
   }
   else if (state->m_colorFrame && state->m_width && state->m_height)
   {
      if (msg.count < msg.maxEntryCount)
      {
         msg.entries[msg.count] = {};
         msg.entries[msg.count].id = { { endpointId, 0 } };
         msg.entries[msg.count].overrideId = dmdId.id;
         msg.entries[msg.count].width = state->m_width;
         msg.entries[msg.count].height = state->m_cropBars ? state->m_height / 2 : state->m_height; // [serum-16high fix]
         msg.entries[msg.count].hardware = CTLPI_DISPLAY_HARDWARE_RGB_LED;
         msg.entries[msg.count].frameFormat = CTLPI_DISPLAY_FORMAT_SRGB888;
         msg.entries[msg.count].GetRenderFrame = &GetRenderFrame;
      }
      msg.count++;
   }
   if (state->m_colorFrame64 && state->m_width64)
   {
      if (msg.count < msg.maxEntryCount)
      {
         msg.entries[msg.count] = {};
         msg.entries[msg.count].id = { { endpointId, 1 } };
         msg.entries[msg.count].overrideId = dmdId.id;
         msg.entries[msg.count].width = state->m_width64;
         msg.entries[msg.count].height = state->m_cropBars ? 32 : 64; // [serum-16high fix]
         msg.entries[msg.count].hardware = CTLPI_DISPLAY_HARDWARE_RGB_LED;
         msg.entries[msg.count].frameFormat = CTLPI_DISPLAY_FORMAT_SRGB565;
         msg.entries[msg.count].GetRenderFrame = &GetRenderFrame;
      }
      msg.count++;
   }
}

// Select the first DMD with a large enough size that supports frame identification
static void OnDmdSrcChanged(const unsigned int, void*, void*)
{
   if (pSerum == nullptr)
      return;
   std::lock_guard<std::mutex> lock(sourceMutex);
   dmdId.id.id = 0;
   GetDisplaySrcMsg getSrcMsg = { 0, 0, nullptr };
   msgApi->BroadcastMsg(endpointId, getDmdSrcId, &getSrcMsg);
   if (getSrcMsg.count == 0)
      return;
   getSrcMsg = { getSrcMsg.count, 0, new DisplaySrcId[getSrcMsg.count] };
   msgApi->BroadcastMsg(endpointId, getDmdSrcId, &getSrcMsg);
   for (unsigned int i = 0; i < getSrcMsg.count; i++)
   {
      if (getSrcMsg.entries[i].GetIdentifyFrame != nullptr && getSrcMsg.entries[i].width >= 128)
      {
         dmdId = getSrcMsg.entries[i];
         break;
      }
   }
   delete[] getSrcMsg.entries;
}

static void StopColorization()
{
   // TODO this is somewhat slow as this will block for up to 16ms => use a condition variable
   isRunning = false;
   if (colorizeThread.joinable())
      colorizeThread.join();
   if (pSerum)
   {
      delete state;
      state = nullptr;
      pSerum = nullptr;
      Serum_Dispose();
      msgApi->BroadcastMsg(endpointId, onDmdSrcChangedId, nullptr);
   }
   dmdId.id.id = 0;
}

static void OnControllerGameStart(const unsigned int eventId, void* userData, void* msgData)
{
   StopColorization();
   // Setup Serum on the selected DMD
   const CtlOnGameStartMsg* msg = static_cast<const CtlOnGameStartMsg*>(msgData);
   assert(msg != nullptr && msg->gameId != nullptr);
   if (serumPathProp_Get()[0] == '\0')
   {
      VPXTableInfo tableInfo;
      vpxApi->GetTableInfo(&tableInfo);
      std::filesystem::path tablePath = tableInfo.path;
      string path = find_case_insensitive_directory_path(tablePath.parent_path().string() + PATH_SEPARATOR_CHAR + "pinmame" + PATH_SEPARATOR_CHAR + "altcolor");
      if (!path.empty())
         serumPathProp_Set(path.c_str());
   }
   pSerum = Serum_Load(serumPathProp_Get(), msg->gameId, FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES);
   OnDmdSrcChanged(onDmdSrcChangedId, nullptr, nullptr);
   if (pSerum)
   {
      isRunning = true;
      colorizeThread = std::thread(ColorizeThread);
   }
}

static void OnControllerGameEnd(const unsigned int eventId, void* userData, void* msgData)
{
   StopColorization();
}

}

using namespace Serum;

MSGPI_EXPORT void MSGPIAPI SerumPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
   msgApi = api;
   endpointId = sessionId;

   // Request and setup shared login API
   LPISetup(endpointId, msgApi);

   msgApi->RegisterSetting(endpointId, &serumPathProp);

   unsigned int getVpxApiId = msgApi->GetMsgID(VPXPI_NAMESPACE, VPXPI_MSG_GET_API);
   msgApi->BroadcastMsg(endpointId, getVpxApiId, &vpxApi);
   msgApi->ReleaseMsgID(getVpxApiId);

   onDmdTrigger = msgApi->GetMsgID("Serum", "OnDmdTrigger");
   msgApi->SubscribeMsg(endpointId, onControllerGameStartId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_START), OnControllerGameStart, nullptr);
   msgApi->SubscribeMsg(endpointId, onControllerGameEndId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_EVT_ON_GAME_END), OnControllerGameEnd, nullptr);
   msgApi->SubscribeMsg(endpointId, onDmdSrcChangedId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_ON_SRC_CHG_MSG), OnDmdSrcChanged, nullptr);
   msgApi->SubscribeMsg(endpointId, getDmdSrcId = msgApi->GetMsgID(CTLPI_NAMESPACE, CTLPI_DISPLAY_GET_SRC_MSG), OnGetRenderDMDSrc, nullptr);
}

MSGPI_EXPORT void MSGPIAPI SerumPluginUnload()
{
   StopColorization();
   msgApi->UnsubscribeMsg(getDmdSrcId, OnGetRenderDMDSrc);
   msgApi->UnsubscribeMsg(onDmdSrcChangedId, OnDmdSrcChanged);
   msgApi->UnsubscribeMsg(onControllerGameStartId, OnControllerGameStart);
   msgApi->UnsubscribeMsg(onControllerGameEndId, OnControllerGameEnd);
   msgApi->ReleaseMsgID(onControllerGameStartId);
   msgApi->ReleaseMsgID(onControllerGameEndId);
   msgApi->ReleaseMsgID(onDmdTrigger);
   msgApi->ReleaseMsgID(onDmdSrcChangedId);
   msgApi->ReleaseMsgID(getDmdSrcId);
   msgApi = nullptr;
}
