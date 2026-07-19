// smb3 identify-stream experiment: boot the fork's libpinmame headless with the real
// smb3 ROM, receive DMD frames via the legacy OnDisplayUpdated callback (this is the
// VBLANK-SAMPLED path: core_dmd_video_update -> core_dmd_update_identify, same as
// classic VPinMAME that colorizations are authored against), feed each frame to the
// device-generation libserum, and count new/same/nof exactly like the Android plugin.
// Also logs each frame's CRC32 (pre-colorize!) + value mask for offline membership
// tests against the .cROMc hash table.
//
// usage: harness <vpmPath> <altcolorPath> <rom> <seconds>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdarg>
#include <atomic>
#include <chrono>
#include <thread>
#include "libpinmame.h"
#include "serum-decode.h"
#include "serum.h"
#include "pluginsim.h"
static PluginSim g_sim;

static uint32_t crcTable[256];
static void CrcInit() {
   for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crcTable[i] = c;
   }
}
static uint32_t Crc32(const uint8_t* p, unsigned int n) {
   uint32_t c = 0xffffffffu;
   for (unsigned int i = 0; i < n; i++) c = (c >> 8) ^ crcTable[(p[i] ^ c) & 0xFF];
   return ~c;
}

// raw PWM ring peek (diagnostic accessor added to core.c)
extern "C" const uint8_t* core_dmd_peek_raw_ring(const int displayIndex, unsigned int* nFrames, unsigned int* rawFrameSize, unsigned int* nextFrame, unsigned int* frameIndex);

static std::atomic<bool> g_ringRun(false);
static void RingSampler() {
   FILE* f = fopen("subframes.bin", "wb");
   if (!f) return;
   unsigned int lastIdx = 0;
   bool first = true;
   while (g_ringRun) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      unsigned int nFrames, rawSize, nextFrame, frameIndex;
      const uint8_t* ring = core_dmd_peek_raw_ring(0, &nFrames, &rawSize, &nextFrame, &frameIndex);
      if (!ring) continue;
      if (first) { first = false; lastIdx = frameIndex; fprintf(stderr, "ring: nFrames=%u rawSize=%u\n", nFrames, rawSize); continue; }
      unsigned int nNew = frameIndex - lastIdx;
      if (nNew == 0) continue;
      if (nNew > nFrames) { fprintf(stderr, "ring overrun: missed %u frames\n", nNew - nFrames); nNew = nFrames; lastIdx = frameIndex - nFrames; }
      // ring slot of frame with index (frameIndex-1) is (nextFrame-1) mod nFrames
      for (unsigned int k = 0; k < nNew; k++) {
         const unsigned int idx = lastIdx + k;                     // absolute submission index
         const unsigned int slot = (nextFrame + nFrames - nNew + k) % nFrames;
         fwrite(&idx, 4, 1, f);
         fwrite(ring + slot * rawSize, 1, rawSize, f);
      }
      lastIdx = frameIndex;
   }
   fclose(f);
}

static Serum_Frame_Struc* g_pSerum = nullptr;
static std::atomic<int> g_new(0), g_same(0), g_nof(0), g_frames(0), g_fallback(0);
static std::atomic<int> g_sceneUpd(0), g_rotUpd(0);
#include <mutex>
static std::mutex g_serumMutex;
static bool g_hasAnim = false;
static std::chrono::steady_clock::time_point g_animNext;
static std::atomic<bool> g_pumpRun(false);
// emulate the FIXED plugin: mask the low word of colorize/rotate returns as the
// delay, pump Serum_Rotate at those ticks (drives rotations AND scene playback)
static void RotatePump() {
   while (g_pumpRun) {
      std::this_thread::sleep_for(std::chrono::microseconds(16666));
      std::lock_guard<std::mutex> lock(g_serumMutex);
      if (!g_hasAnim) continue;
      const auto now = std::chrono::steady_clock::now();
      while (g_hasAnim && g_animNext <= now) {
         const uint32_t rc = Serum_Rotate();
         const uint32_t delayMs = rc & 0xffff;
         if ((rc & FLAG_RETURNED_V2_SCENE) && (g_pSerum->flags & (FLAG_RETURNED_32P_FRAME_OK | FLAG_RETURNED_64P_FRAME_OK)))
            g_sceneUpd++;
         else if (rc & (FLAG_RETURNED_V2_ROTATED32 | FLAG_RETURNED_V2_ROTATED64))
            g_rotUpd++;
         g_sim.OnRotated(rc, g_pSerum);
         if (delayMs == 0) { g_hasAnim = false; break; }
         g_animNext += std::chrono::milliseconds(delayMs);
      }
   }
}
static FILE* g_crcLog = nullptr;
static uint8_t g_frameCopy[128 * 32];
static std::chrono::steady_clock::time_point g_t0;

static void OnDisplayAvailable(int index, int displayCount, PinmameDisplayLayout* p_layout, void* ud) {
   fprintf(stderr, "display available: index=%d count=%d type=%d %dx%d depth=%d\n",
      index, displayCount, (int)p_layout->type, p_layout->width, p_layout->height, p_layout->depth);
}

static bool g_lumMode = false;      // feed Serum trunc(15*lum) like Windows dmddevice GTS3 path
static uint8_t g_lastQuant[128 * 32];
static bool g_lineDouble = false;   // DE 128x16: packs are authored at 128x32 (line-doubled)
static bool g_hex16 = false;        // log raw 128x16 frames as hex for offline transform search
static uint8_t g_doubled[128 * 32];
static uint8_t g_lastHex[128 * 16];
static void OnDisplayUpdated(int index, void* p_data, PinmameDisplayLayout* p_layout, void* ud) {
   if (!p_data) return; // unchanged frame
   int w = p_layout->width, h = p_layout->height;
   if (w == 128 && h == 16 && g_hex16) {
      if (memcmp(g_lastHex, p_data, 128 * 16) == 0) return;
      memcpy(g_lastHex, p_data, 128 * 16);
      if (g_crcLog) {
         const uint8_t* pix = static_cast<const uint8_t*>(p_data);
         for (int i = 0; i < 128 * 16; i++) fprintf(g_crcLog, "%01x", pix[i] & 15);
         fprintf(g_crcLog, "\n");
      }
      g_frames++;
      return;
   }
   if (w == 128 && h == 16 && g_lineDouble) {
      // center the 16 rows in a 128x32 canvas (rows 8..23), black elsewhere
      const uint8_t* srcRows = static_cast<const uint8_t*>(p_data);
      memset(g_doubled, 0, sizeof(g_doubled));
      memcpy(g_doubled + 8 * 128, srcRows, 16 * 128);
      p_data = g_doubled;
      h = 32;
   }
   if (w != 128 || h != 32) return;
   uint32_t crcR = 0, crcS = 0;
   if (g_lumMode) {
      // BRIGHTNESS mode delivers b = trunc(255*lum).
      // Candidate 16-level quantizations of the same luminance:
      //   trunc(15*lum)      == b/17            (Windows ControllerDmdDevice legacy path)
      //   round(15*lum)      ~= (b*15 + 127)/255
      //   trunc(255*lum)>>4  == b>>4            (byte-plane style)
      uint8_t* p = static_cast<uint8_t*>(p_data);
      static uint8_t qr[128 * 32], qs[128 * 32];
      for (int i = 0; i < w * h; i++) {
         const uint8_t b = p[i];
         qr[i] = (uint8_t)((b * 15 + 127) / 255);
         qs[i] = b >> 4;
         p[i] = b / 17;
      }
      if (memcmp(g_lastQuant, p, w * h) == 0) return; // dedupe at the 16-level domain
      memcpy(g_lastQuant, p, w * h);
      crcR = Crc32(qr, w * h);
      crcS = Crc32(qs, w * h);
   }
   // copy BEFORE Serum_Colorize: libserum mutates the input buffer in place
   memcpy(g_frameCopy, p_data, w * h);
   unsigned int valMask = 0;
   for (int i = 0; i < w * h; i++) valMask |= 1u << (g_frameCopy[i] & 15);
   const uint32_t crc = Crc32(g_frameCopy, w * h);
   g_frames++;
   const char* res = "n/a";
   if (g_pSerum) {
      std::lock_guard<std::mutex> lock(g_serumMutex);
      const uint32_t rc = Serum_Colorize(static_cast<uint8_t*>(p_data));
      if (rc == IDENTIFY_NO_FRAME) { g_nof++; res = "nof"; }
      else if (rc == IDENTIFY_SAME_FRAME) { g_same++; res = "same"; }
      else {
         if (g_pSerum->frameID == 0xfffffffd) { g_fallback++; res = "gray"; } // unknown-frame grayscale fallback
         else { g_new++; res = "new"; }
         g_sim.OnColorized(rc, g_pSerum);
         const uint32_t delayMs = rc & 0xffff;   // low word only; high bits are FLAG_RETURNED_V2_* markers
         g_hasAnim = delayMs != 0;
         if (g_hasAnim) g_animNext = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
      }
   }
   const long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_t0).count();
   if (g_crcLog) fprintf(g_crcLog, "%ld %s %04x %08x %08x %08x\n", ms, res, valMask, crc, crcR, crcS);
}

static void OnLogMessage(PINMAME_LOG_LEVEL logLevel, const char* format, va_list args, void* ud) {
   // keep pinmame quiet; errors only
   if (logLevel >= PINMAME_LOG_LEVEL_ERROR) { vfprintf(stderr, format, args); fprintf(stderr, "\n"); }
}

int main(int argc, char** argv) {
   if (argc < 5) { fprintf(stderr, "usage: harness <vpmPath> <altcolorPath> <rom> <seconds>\n"); return 2; }
   const char* vpmPath = argv[1];
   const char* altcolor = argv[2];
   const char* rom = argv[3];
   const int seconds = atoi(argv[4]);

   CrcInit();
   g_crcLog = fopen("frames.log", "w");

   g_pSerum = Serum_Load(altcolor, rom, FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES);
   fprintf(stderr, g_pSerum ? "serum loaded: v%d\n" : "serum load FAILED\n", g_pSerum ? (int)g_pSerum->SerumVersion : 0);
   if (argc > 6) {
      const int skip = atoi(argv[6]);
      const int timeoutMs = argc > 7 ? atoi(argv[7]) : 0;
      if (skip > 0) Serum_SetMaximumUnknownFramesToSkip((uint8_t)skip);
      if (timeoutMs > 0) Serum_SetIgnoreUnknownFramesTimeout((uint16_t)timeoutMs);
      fprintf(stderr, "fallback: skip=%d timeout=%dms\n", skip, timeoutMs);
   }
   g_t0 = std::chrono::steady_clock::now();

   PinmameConfig config = {
      PINMAME_AUDIO_FORMAT_FLOAT,
      48000,
      "",
      nullptr,                // OnStateUpdated
      &OnDisplayAvailable,
      &OnDisplayUpdated,      // activates the vblank identify path
      nullptr,                // OnAudioAvailable
      nullptr,                // OnAudioUpdated
      nullptr,                // OnMechAvailable
      nullptr,                // OnMechUpdated
      nullptr,                // OnSolenoidUpdated
      nullptr,                // OnConsoleDataUpdated
      nullptr,                // IsKeyPressed
      &OnLogMessage,
      nullptr,                // OnSoundCommand
   };
   snprintf((char*)config.vpmPath, PINMAME_MAX_PATH, "%s/", vpmPath);
   PinmameSetConfig(&config);
   g_lumMode = (argc > 5 && strcmp(argv[5], "lum") == 0);
   g_lineDouble = (argc > 5 && strcmp(argv[5], "dbl") == 0);
   if (g_lineDouble) fprintf(stderr, "mode: 128x16 line-doubled to 128x32\n");
   g_hex16 = (argc > 5 && strcmp(argv[5], "hex16") == 0);
   if (g_hex16) fprintf(stderr, "mode: raw 128x16 hex logging\n");
   PinmameSetDmdMode(g_lumMode ? PINMAME_DMD_MODE_BRIGHTNESS : PINMAME_DMD_MODE_RAW);
   fprintf(stderr, "mode: %s\n", g_lumMode ? "luminance/16 (Windows GTS3 dmddevice)" : "identify raw");
   PinmameSetHandleKeyboard(0);
   PinmameSetHandleMechanics(0);

   const PINMAME_STATUS status = PinmameRun(rom);
   if (status != PINMAME_STATUS_OK) { fprintf(stderr, "PinmameRun failed: %d\n", (int)status); return 1; }

   g_ringRun = true;
   std::thread ringThread(RingSampler);
   g_pumpRun = true;
   std::thread pumpThread(RotatePump);

   auto dumpPpm = [](const char* name, const uint16_t* f, int w, int h) {
      if (!f || !w) return;
      FILE* im = fopen(name, "wb");
      if (!im) return;
      fprintf(im, "P6\n%d %d\n255\n", w, h);
      for (int i = 0; i < w * h; i++) {
         const uint16_t v = f[i];
         const uint8_t rgb[3] = { (uint8_t)(((v >> 11) & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v & 31) << 3) };
         fwrite(rgb, 1, 3, im);
      }
      fclose(im);
   };
   for (int s = 0; s < seconds; s++) {
      for (int half = 0; half < 2; half++) {
         std::this_thread::sleep_for(std::chrono::milliseconds(500));
         // snapshot libserum's own colorized output (bypasses any plugin logic)
         std::lock_guard<std::mutex> lock(g_serumMutex);
         if (g_pSerum) {
            char name[128];
            // dump what the PLUGIN would hand the renderer (published planes)
            snprintf(name, sizeof(name), "frames/p%04d_32.ppm", s * 2 + half);
            if (g_sim.colorFrame32 && g_sim.width32)
               dumpPpm(name, reinterpret_cast<const uint16_t*>(g_sim.colorFrame32), g_sim.width32, 32);
            snprintf(name, sizeof(name), "frames/p%04d_64.ppm", s * 2 + half);
            if (g_sim.colorFrame64 && g_sim.width64)
               dumpPpm(name, reinterpret_cast<const uint16_t*>(g_sim.colorFrame64), g_sim.width64, 64);
            fprintf(stderr, "[dump %04d] frameID=%u flags=%02x w32=%u w64=%u sim32=%u sim64=%u synth64=%d\n",
               s * 2 + half, g_pSerum->frameID, (unsigned)g_pSerum->flags,
               g_pSerum->width32, g_pSerum->width64, g_sim.width32, g_sim.width64, g_sim.synth64 ? 1 : 0);
         }
      }
      fprintf(stderr, "[t=%02ds] frames=%d new=%d same=%d nof=%d gray=%d scene=%d rot=%d\n",
         s + 1, g_frames.load(), g_new.load(), g_same.load(), g_nof.load(), g_fallback.load(),
         g_sceneUpd.load(), g_rotUpd.load());
   }
   g_pumpRun = false;
   pumpThread.join();
   g_ringRun = false;
   ringThread.join();
   PinmameStop();
   if (g_crcLog) fclose(g_crcLog);
   fprintf(stderr, "FINAL frames=%d new=%d same=%d nof=%d gray=%d\n", g_frames.load(), g_new.load(), g_same.load(), g_nof.load(), g_fallback.load());
   return 0;
}
