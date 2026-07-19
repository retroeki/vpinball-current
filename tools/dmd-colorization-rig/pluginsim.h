// Faithful port of the serum plugin's ColorizationState publication pipeline
// (plugins/serum/serum.cpp as deployed today): plugin-owned double buffers,
// UpdateFrame32/64V2 on colorize flags, synthesis of the missing plane, and the
// rotate-branch scene/rotation updates. Lets us dump EXACTLY what the plugin
// would hand the VPX renderer.
#pragma once
#include <cstdint>
#include <cstring>

struct PluginSim {
   uint16_t* own32[2] = { nullptr, nullptr };
   uint16_t* own64[2] = { nullptr, nullptr };
   int front32 = 0, front64 = 0;
   unsigned alloc32 = 0, alloc64 = 0;
   unsigned width32 = 0, width64 = 0;
   uint8_t* colorFrame32 = nullptr;
   uint8_t* colorFrame64 = nullptr;
   bool synth32 = false, synth64 = false;
   unsigned frameId = 0;

   void Ensure32(unsigned w) {
      if (alloc32 != w) { delete[] own32[0]; delete[] own32[1]; own32[0] = new uint16_t[32 * w](); own32[1] = new uint16_t[32 * w](); alloc32 = w; }
   }
   void Ensure64(unsigned w) {
      if (alloc64 != w) { delete[] own64[0]; delete[] own64[1]; own64[0] = new uint16_t[64 * w](); own64[1] = new uint16_t[64 * w](); alloc64 = w; }
   }
   void Publish32(const uint16_t* src, unsigned w) {
      Ensure32(w);
      uint16_t* back = own32[1 - front32];
      memcpy(back, src, 32 * w * sizeof(uint16_t));
      front32 = 1 - front32; width32 = w;
      colorFrame32 = reinterpret_cast<uint8_t*>(own32[front32]); frameId++;
   }
   void Publish64(const uint16_t* src, unsigned w) {
      Ensure64(w);
      uint16_t* back = own64[1 - front64];
      memcpy(back, src, 64 * w * sizeof(uint16_t));
      front64 = 1 - front64; width64 = w;
      colorFrame64 = reinterpret_cast<uint8_t*>(own64[front64]); frameId++;
   }
   void Update32(const Serum_Frame_Struc* s) { if (s->width32 > 0) { Publish32(s->frame32, s->width32); synth32 = false; } }
   void Update64(const Serum_Frame_Struc* s) { if (s->width64 > 0) { Publish64(s->frame64, s->width64); synth64 = false; } }
   void Synth64From32() {
      if (width32 == 0 || width64 == 0 || width64 != width32 * 2 || !colorFrame32) return;
      const uint16_t* src = reinterpret_cast<const uint16_t*>(colorFrame32);
      Ensure64(width64);
      uint16_t* dst = own64[1 - front64];
      for (unsigned y = 0; y < 64; y++) {
         const uint16_t* srow = src + (y / 2) * width32;
         uint16_t* d = dst + y * width64;
         for (unsigned x = 0; x < width32; x++) { d[x * 2] = srow[x]; d[x * 2 + 1] = srow[x]; }
      }
      front64 = 1 - front64;
      colorFrame64 = reinterpret_cast<uint8_t*>(own64[front64]);
      synth64 = true; frameId++;
   }
   void Synth32From64() {
      if (width32 == 0 || width64 == 0 || width64 != width32 * 2 || !colorFrame64) return;
      const uint16_t* src = reinterpret_cast<const uint16_t*>(colorFrame64);
      Ensure32(width32);
      uint16_t* dst = own32[1 - front32];
      for (unsigned y = 0; y < 32; y++) {
         const uint16_t* srow = src + (y * 2) * width64;
         uint16_t* d = dst + y * width32;
         for (unsigned x = 0; x < width32; x++) d[x] = srow[x * 2];
      }
      front32 = 1 - front32;
      colorFrame32 = reinterpret_cast<uint8_t*>(own32[front32]);
      synth32 = true; frameId++;
   }
   // mirrors the plugin's new-frame branch after Serum_Colorize (rc = return, s = pSerum)
   void OnColorized(uint32_t rc, const Serum_Frame_Struc* s) {
      const bool ok32 = (s->flags & FLAG_RETURNED_32P_FRAME_OK) != 0;
      const bool ok64 = (s->flags & FLAG_RETURNED_64P_FRAME_OK) != 0;
      if (ok32) Update32(s);
      if (ok64) Update64(s);
      if (ok32 && !ok64) Synth64From32();
      else if (ok64 && !ok32) Synth32From64();
   }
   // mirrors the plugin's rotate branch result handling
   void OnRotated(uint32_t nextrot, const Serum_Frame_Struc* s) {
      const bool scene = (nextrot & FLAG_RETURNED_V2_SCENE) != 0;
      const bool rot32 = (nextrot & FLAG_RETURNED_V2_ROTATED32) != 0 || (scene && (s->flags & FLAG_RETURNED_32P_FRAME_OK) != 0);
      const bool rot64 = (nextrot & FLAG_RETURNED_V2_ROTATED64) != 0 || (scene && (s->flags & FLAG_RETURNED_64P_FRAME_OK) != 0);
      if (rot32) Update32(s);
      if (rot64) Update64(s);
      if (rot32 && !rot64 && synth64) Synth64From32();
      else if (rot64 && !rot32 && synth32) Synth32From64();
   }
};
