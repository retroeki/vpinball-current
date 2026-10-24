// license:GPLv3+

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <unordered_map>

#include "ReplayState.h"
#include "plugins/ControllerPlugin.h"

class Player;
class HitBall;
class HitFlipper;
namespace VPX { class Sound; }

// Records resulting gameplay state (not inputs): ball transforms on a fixed 8 ms
// grid, flipper angles per 1 ms physics cycle while moving, ball create/destroy
// events, displays, sounds. Replay is playback of this state, never a
// re-simulation (see docs/replay-system-spec.md in the app repo for why).
//
// The FULL session is captured (revised 2026-07-10, was a 90 s ring): sealed
// chunks are handed to a writer thread that appends them to <finalPath>.tmp as
// play proceeds, so RAM holds only the open chunk regardless of session length
// and the game thread never touches disk. On teardown the header is finalized
// and the temp is renamed over finalPath, so finalPath always holds the last
// COMPLETE session and a crash mid-session leaves only a .tmp that fails
// header validation. The app splits the session file into per-game clips
// afterwards (segmentation per docs/replay-moments-spec.md).
//
// Master switch: Standalone/ReplayRecord (Settings). When off, Player never
// constructs this object, so the whole system costs nothing.
class ReplayRecorder final
{
public:
   ReplayRecorder(Player *player, const string &finalPath);
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

   static constexpr uint32_t BALL_SAMPLE_INTERVAL_MS = 8;    // 125 Hz ball transforms
   // 256 KB: every chunk seal costs a keyframe (state snapshot + re-emitted
   // display payloads, ~32 KB on big tables). 64 KB chunks turned over so
   // fast on RGB-DMD tables (FlexDMD) that keyframes were a third of the file.
   static constexpr size_t CHUNK_SIZE = 256 * 1024;
   // Display sources cap at ~30 fps in the recording: FlexDMD bumps frameId
   // at render rate (~140/s observed) and an RGB888 DMD frame is 12 KB that
   // byte-RLE cannot compress — uncapped this alone was multiple MB/s.
   static constexpr uint32_t DISPLAY_FRAME_MIN_INTERVAL_MS = 33;
   // v4 (2026-07-13): state registry gained plunger position, primitive
   // disable-lighting + position, flasher alpha/color, light color/intensity,
   // material opacity/color, bumper ring and slingshot frames. Slot numbers
   // shifted, so v3 files are rejected (re-record), same as every prior bump.
   // v5 (2026-10, Pocket Pinball tournaments part 4, 3.7): a u16-length block of
   // UTF-8 key=value lines follows the 20-byte header (attemptId, tableSha1,
   // appVersion, engineVersion, startEpochMs, playfield). Chunks start after it.
   // Readers accept 4 and 5.
   static constexpr uint16_t FORMAT_VERSION = 5;
   // Marathon-session safety valve: stop appending past this (~4.3 h at ~65 KB/s);
   // the recording stays valid, just truncated.
   static constexpr uint64_t MAX_SESSION_BYTES = 1ull << 30;

   // Record types (layouts unchanged since v3; v4 only changes state-slot numbering)
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
      REC_DISPLAY_DEF = 9,  // t u32, key u8, id u64, groupId u64, overrideId u64, w u16, h u16, hardware u32, frameFormat u8
      REC_SEG_DEF = 10,     // t u32, key u8, id u64, groupId u64, hardware u32, nElements u8, elementType u8 x nElements
      REC_DISPLAY_FRAME = 11, // t u32, key u8, flags u8 (bit0 rle), len u32, payload (LUM32F stored as LUM8; 888/565 raw)
      REC_SEG_FRAME = 12,   // t u32, key u8, flags u8 (bit0 rle), len u16, payload (u8 luminances, nElements*16)
   };

private:
   struct Chunk
   {
      uint32_t firstTimeMs = 0;
      uint32_t lastTimeMs = 0;
      std::vector<uint8_t> data;
   };

   void OpenChunk(uint32_t timeMs);          // seals current chunk to the writer queue, starts a new one with a keyframe
   void EnsureSpace(size_t bytes, uint32_t timeMs);
   void WriteKeyframe(uint32_t timeMs);
   void WriterLoop();                        // writer thread: drains the queue to the .tmp file
   void AppendChunkToFile(const Chunk &c);   // writer thread only
   void FinalizeFile();                      // teardown: patch header, rename .tmp over finalPath
   uint8_t BallId(HitBall *pHitBall);

   // Display / segment sources (PinMAME DMD, FlexDMD, Serum overrides, alphanumeric)
   static void OnSourcesChanged(const unsigned int msgId, void *userData, void *msgData); // any thread: only marks dirty
   void EnumerateSources(uint32_t timeMs);   // game thread (OnFrame), under m_mutex
   void PollDisplaySources(uint32_t timeMs); // game thread (OnFrame), under m_mutex
   void WriteDisplayDef(uint32_t timeMs, uint8_t key, const DisplaySrcId &src);
   void WriteSegDef(uint32_t timeMs, uint8_t key, const SegSrcId &src);
   void WriteDisplayFrame(uint32_t timeMs, uint8_t key, const std::vector<uint8_t> &raw);
   void WriteSegFrame(uint32_t timeMs, uint8_t key, const std::vector<uint8_t> &raw);
   void EmitSourceDefsAndFrames(uint32_t timeMs); // after each keyframe: defs + last known payloads

   // append helpers into m_cur.data
   void PutU8(uint8_t v);
   void PutU16(uint16_t v);
   void PutU32(uint32_t v);
   void PutU64(uint64_t v);
   void PutF32(float v);
   void PutQuat(const float q[4]);
   static void MatrixToQuat(const HitBall *pHitBall, float q[4]);

   Player *const m_player;
   std::mutex m_mutex; // guards the open chunk; hooks append from game + audio threads

   Chunk m_cur;
   bool m_curOpen = false;
   bool m_emittingKeyframe = false;

   // Writer thread: sealed chunks stream to disk so RAM stays flat for any session length
   std::string m_finalPath;
   std::string m_tmpPath;
   std::fstream m_file;
   bool m_fileOk = false;
   std::thread m_writerThread;
   std::mutex m_queueMutex;
   std::condition_variable m_queueCv;
   std::deque<Chunk> m_queue;      // guarded by m_queueMutex
   bool m_writerStop = false;      // guarded by m_queueMutex
   uint32_t m_firstMs = 0;         // writer thread only (joined before FinalizeFile reads them)
   uint32_t m_lastMs = 0;
   uint32_t m_chunkCount = 0;
   uint64_t m_bytesWritten = 0;
   bool m_capWarned = false;

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

   std::atomic<bool> m_sourcesDirty { true };
   unsigned int m_onDisplayChgId = 0, m_getDisplayId = 0, m_onSegChgId = 0, m_getSegId = 0;
   std::vector<DisplaySrcId> m_displaySources;
   std::vector<SegSrcId> m_segSources;
   std::unordered_map<uint64_t, uint8_t> m_displayKeys; // source id -> key (stable across re-enumeration)
   std::unordered_map<uint64_t, uint8_t> m_segKeys;
   // The def as first written per key. Frames MUST match their key's def
   // (payload size is derived from it at parse time), but bus re-enumeration
   // can hand back entries whose geometry doesn't match their id (seen with
   // B2S aux displays: a 14x10 RGB frame arrived under the DMD's key and one
   // corrupt record made the strict loader reject the whole file). Polls
   // validate against the pinned def and keyframes re-emit IT, never the
   // live enumeration entry.
   std::unordered_map<uint8_t, DisplaySrcId> m_displayDefSrc;
   std::unordered_map<uint8_t, SegSrcId> m_segDefSrc;
   std::unordered_map<uint8_t, bool> m_displayShapeWarned;
   std::unordered_map<uint8_t, bool> m_segShapeWarned;
   std::unordered_map<uint8_t, unsigned int> m_lastDisplayFrameId;
   std::unordered_map<uint8_t, unsigned int> m_lastSegFrameId;
   std::unordered_map<uint8_t, uint32_t> m_lastDisplayWriteMs; // per-key ~30 fps write cap
   std::unordered_map<uint8_t, std::vector<uint8_t>> m_lastDisplayPayload; // cached raw payload for keyframe re-emission
   std::unordered_map<uint8_t, std::vector<uint8_t>> m_lastSegPayload;
   std::vector<uint8_t> m_frameScratch; // raw payload build buffer
   std::vector<uint8_t> m_rleScratch;   // rle encode buffer

   uint32_t m_lastBallSampleMs = UINT32_MAX;
};
