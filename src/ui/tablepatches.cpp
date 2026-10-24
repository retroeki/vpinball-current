// license:GPLv3+
#include "ui/tablepatches.h"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <regex>

namespace tablepatches {

const TablePatch kTablePatches[] = {
   // Route the LED score/text to the Led DispReels (b2sParseString picks the REEL glyph
   // table; b2sSendToBG writes leddisp.SetValue) WITHOUT flipping the global `Desktop`
   // flag. Both routines gate on `If Desktop=True Then` (that exact text appears only in
   // those two subs). Forcing the global Desktop=True instead would also skip the init
   // `If Desktop=false` block that hides the reels, leaving 40 backglass reels visible
   // (camera auto-fit glitch). Leaving Desktop=False keeps them hidden; ReelDmd reads
   // reel values regardless of visibility, so the in-app DMD panel still renders the score.
   { "DragonFire.vpx", "If Desktop=True Then", "If True Then", false,
     "DragonFire: route LED display to the (hidden) Led reels without un-hiding them" },
   // RoboCop (Bigus MOD 3.0): the ball dead-stops on the small collidable post Wall15 in the
   // app but not on Windows. Same .vpx, same geometry - the Android/Clang build uses
   // approximate-math float flags (-fapprox-func/-freciprocal-math/-fassociative-math), which
   // tip a marginal resting contact into a stuck one. Zeroing Wall15's friction lets the ball
   // slip off the post (it's a passive post: no Wall15_Hit handler, unreferenced in script) so
   // friction can't hold it. Relies on Surface::put_Friction propagating to live hit objects.
   { "Robocop (Data East 1989)_Bigus(MOD)3.0.vpx", "Sub Table1_Init",
     "Sub Table1_Init\r\n    Wall15.Friction = 0", false,
     "RoboCop: zero Wall15 friction so the ball slips off the post instead of dead-stopping (app-only)" },
   // 4 Queens (EM): the score is driven BOTH by native DispReels (reel1/reel2, members of the
   // DTItems collection) and by ReelDmd, which reads the reel VALUES and paints the score into
   // the in-app ScoreView panel. In the app's forced-Desktop view ShowDT is true, so the table's
   // own `If ShowDT=false Then <hide DTItems>` init block never runs, and every DTItem (reel1,
   // reel2, gamov = the "GAME OVER" textbox, credittxt, match m0..m9, tilt, ball-in-play) renders
   // in the 3D scene, floating over the playfield as a duplicate of the ScoreView panel. Forcing
   // the branch true hides all DTItems; ReelDmd still shows the score because it reads reel values
   // regardless of visibility. Same pattern and rationale as the DragonFire entry above.
   { "4 Queens (Bally 1970).vpx", "If ShowDT=false Then", "If True Then", false,
     "4 Queens: always run the hide-DTItems branch so native reels + gamov/credit/match never render in-scene; ReelDmd owns the score" },
   // Beat Time & Circus (EM, same reel framework): the score reels (PlayerScores/PlayerScoresOn)
   // are driven by relative AddValue, and the ONLY per-new-game ResetToZero is gated behind
   // `If Table1.ShowDT = True` (the ungated reset is load-time only, in Table1_Init). ShowDT is
   // false in our render view, so the reels never zero between games and new pulses pile onto the
   // stale score. Fix: hoist just the two ResetToZero loops OUT of the gate (ungated, immediately
   // before the If), leaving the block's visibility swaps untouched. Regex `\s+` tolerates the
   // exact indentation; anchoring on `...For each obj in PlayerScores\s+obj.ResetToZero` matches
   // ONLY the reset block, never the sibling ShowDT=True visibility-only blocks. ReelDmd reads
   // reel VALUES, so the ScoreView panel keeps updating. Per-table (filename-gated) on purpose:
   // the ~43 EM tables sharing this method vary too much for one safe generic rule.
   { "Beat Time (Williams 1967).vpx",
     "If Table1\\.ShowDT = True then\\s+For each obj in PlayerScores\\s+obj\\.ResetToZero",
     "For each obj in PlayerScores\r\n\t\t\tobj.ResetToZero\r\n\t\tNext\r\n\t\tFor each obj in PlayerScoresOn\r\n\t\t\tobj.ResetToZero\r\n\t\tNext\r\n\t\tIf Table1.ShowDT = True then\r\n\t\t\tFor each obj in PlayerScores\r\n\t\t\t\tobj.ResetToZero",
     true,
     "Beat Time: hoist the score-reel ResetToZero out of the ShowDT=True gate so reels zero each new game" },
   { "Circus (Zaccaria 1977)_Teisen_2.2_burger.vpx",
     "If Table1\\.ShowDT = True then\\s+For each obj in PlayerScores\\s+obj\\.ResetToZero",
     "For each obj in PlayerScores\r\n\t\t\tobj.ResetToZero\r\n\t\tNext\r\n\t\tFor each obj in PlayerScoresOn\r\n\t\t\tobj.ResetToZero\r\n\t\tNext\r\n\t\tIf Table1.ShowDT = True then\r\n\t\t\tFor each obj in PlayerScores\r\n\t\t\t\tobj.ResetToZero",
     true,
     "Circus: hoist the score-reel ResetToZero out of the ShowDT=True gate so reels zero each new game" },
   // Dark Chaos (GLF original, featured table): mirror the 2-character ball-number
   // display into the FlexDMD segment slots 32..33 (players fill 0..31 of the 2x16),
   // so the FlexDMD plugin can publish it as an extra seg group for the per-table
   // ScoreView layout (red digits under the dot display, matching the desktop look).
   // The GLF write path is under On Error Resume Next, so with the virtual segment
   // DMD off the extra writes are inert, exactly like the player displays today.
   // Three coordinated edits:
   // 1) grow the mirror array from 32 to 38 slots (also matches the 38-slot
   //    m_segData the native FlexDMD.SetSegments copies),
   // 2) widen the enable-time zeroing loop accordingly (regex tolerates indentation),
   // 3) give the ball display a FlexDMD mirror index right where the author set its
   //    B2S mirror index.
   { "Dark Chaos (apophis 2025) 2.0.vpx",
     "Dim glf_flex_alphadmd_segments(31)",
     "Dim glf_flex_alphadmd_segments(37)", false,
     "Dark Chaos: grow FlexDMD segment mirror array to 38 slots for the ball display" },
   { "Dark Chaos (apophis 2025) 2.0.vpx",
     "For i = 0 To 31(\\s+glf_flex_alphadmd_segments\\(i\\) = 0)",
     "For i = 0 To 37$1", true,
     "Dark Chaos: zero all 38 FlexDMD mirror slots on virtual segment DMD enable" },
   { "Dark Chaos (apophis 2025) 2.0.vpx",
     "segment_display_ball.ExternalB2SSegmentIndex = 1",
     "segment_display_ball.ExternalB2SSegmentIndex = 1\r\n    segment_display_ball.ExternalFlexDmdSegmentIndex = 32", false,
     "Dark Chaos: mirror the ball-number display into FlexDMD segment slots 32..33" },
   // 4) Ball-drain light show: a cached show step can list the same fade cache name for
   //    two lights, so the second shows_added.Add throws VBS 457 (duplicate dictionary
   //    key). The error aborts DelayTick before its delayQueue.Remove, so the same
   //    delayed callback re-fires every frame - an endless error storm. Guarding the Add
   //    is behavior-preserving: shows_added is a name-set consumed by GlfShowStepHandler
   //    (duplicates collapse), and the duplicate's re-created fade show goes through
   //    GlfRunningShow.init's own stop-and-replace path (StopRunningShow removes the
   //    glf_running_shows key before the re-Add).
   { "Dark Chaos (apophis 2025) 2.0.vpx",
     "shows_added.Add cache_name, True",
     "If Not shows_added.Exists(cache_name) Then shows_added.Add cache_name, True", false,
     "Dark Chaos: tolerate duplicate fade cache names in a light-show step (VBS 457 storm on ball drain)" },
   // Cactus Canyon TTNZ v2.0 (both builds) were written against a custom controller.vbs
   // whose LoadVBSFiles takes (VPM version, VBS file, VBS version) and whose LoadController
   // takes the table type alone. The bundled controller.vbs has LoadVBSFiles(VBSfile) and
   // LoadController(TableType, VPMver, VBSfile, VBSver), so the first call failed with 450
   // in global code and the table never started (crash triage 2026-10-23, cluster N, 7
   // reports). LoadVPM does both steps with the same arguments.
   { "Cactus Canyon (Bally 1998) TTNZ v2.0.vpx",
     "LoadVBSFiles \"02800000\", \"WPC\\.VBS\", 3\\.52\\s+If PROC = 1 Then\\s+LoadController\\(\"PROC\"\\)\\s+Else\\s+LoadController\\(\"VPM\"\\)\\s+End If",
     "LoadVPM \"02800000\", \"WPC.VBS\", 3.52", true,
     "Cactus Canyon TTNZ: load VPM through the bundled controller.vbs (custom LoadVBSFiles/LoadController signatures)" },
   { "Cactus Canyon Continued _TTNZ  v2.0.vpx",
     "LoadVBSFiles \"02800000\", \"WPC\\.VBS\", 3\\.52\\s+If PROC = 1 Then\\s+LoadController\\(\"PROC\"\\)\\s+Else\\s+LoadController\\(\"VPM\"\\)\\s+End If",
     "LoadVPM \"02800000\", \"WPC.VBS\", 3.52", true,
     "Cactus Canyon Continued TTNZ: load VPM through the bundled controller.vbs (custom LoadVBSFiles/LoadController signatures)" },
   // The Continued build ships set for P-ROC hardware (PROC = 1), which has no Android
   // counterpart; the table's own PROC = 0 mode runs the cc_13 ROM through VPinMAME.
   { "Cactus Canyon Continued _TTNZ  v2.0.vpx", "Const PROC = 1", "Const PROC = 0", false,
     "Cactus Canyon Continued TTNZ: use the table's VPinMAME mode instead of P-ROC hardware" },
   // Ramones (both builds): a typo, '.' for ',' between PlaySoundAt's arguments. Windows
   // compiles the line and would only fail if it ran; Wine's grammar rejects it at compile
   // time (1005), so the table cannot start (cluster O, 7 reports; wine_harness case o_*).
   { "Ramones (HauntFreaks 2021).vpx", "PlaySoundAt \"flip_hit_1\". Rubber32", "PlaySoundAt \"flip_hit_1\", Rubber32", false,
     "Ramones: fix the '.' typo between PlaySoundAt arguments (Wine compile error 1005)" },
   { "Ramones (HauntFreaks 2021) v2.0.vpx", "PlaySoundAt \"flip_hit_1\". Rubber32", "PlaySoundAt \"flip_hit_1\", Rubber32", false,
     "Ramones v2.0: fix the '.' typo between PlaySoundAt arguments (Wine compile error 1005)" },
   // rpdr: Include("../scripts/core.vbs") reads core.vbs from a Scripts folder next to the
   // Tables folder, the Windows VPX layout. On the device the VPM scripts live inside the app,
   // so OpenTextFile failed (E_FAIL) in global code. GetTextFile serves the bundled core.vbs.
   { "rpdr.vpx", "Include(\"../scripts/core.vbs\")", "ExecuteGlobal GetTextFile(\"core.vbs\")", false,
     "rpdr: load core.vbs from the app's bundled scripts instead of ../scripts/" },
   // Darkest Dungeon 3.06: CheckPupVersion (called from init) opens PuPlayer.GetRoot's
   // parent folder to read the Windows PinUPDisplayX.exe version. Without the PuP pack
   // GetRoot is "" on the device (plugins/pup PUPManager::EnsureRootPath), GetFolder("")
   // fails with 76 and the rest of Table1_Init is skipped. The check only shows a reminder
   // when the exe is old, so skip it when there is no PuP folder; the table then starts.
   { "DarkestDungeon_3.06.vpx", "set pFolder = fso.GetFolder(PuPlayer.getroot)",
     "If PuPlayer.getroot = \"\" Then Exit Sub\r\n       set pFolder = fso.GetFolder(PuPlayer.getroot)", false,
     "Darkest Dungeon: skip the PinUP exe version check when the PuP pack is not installed" },
};
const size_t kTablePatchCount = sizeof(kTablePatches) / sizeof(kTablePatches[0]);

namespace {
std::string Basename(const std::string& path) {
   size_t end = path.size();
   while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\'))
      --end; // ignore trailing separators so "foo/bar/" -> "bar"
   const size_t s = path.find_last_of("/\\", end == 0 ? std::string::npos : end - 1);
   const size_t start = (s == std::string::npos) ? 0 : s + 1;
   return path.substr(start, end - start);
}
std::string ToLower(std::string s) {
   for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   return s;
}
int ReplaceAllLiteral(std::string& s, const std::string& find, const std::string& repl) {
   if (find.empty()) return 0;
   int n = 0;
   size_t pos = 0;
   while ((pos = s.find(find, pos)) != std::string::npos) {
      s.replace(pos, find.size(), repl);
      pos += repl.size();
      ++n;
   }
   return n;
}
} // namespace

std::string ApplyTableSpecificPatches(const std::string& script, const std::string& tableFilename,
   const TablePatch* patches, size_t count,
   std::vector<std::pair<std::string, int>>* applied) {
   const std::string base = ToLower(Basename(tableFilename));
   if (base.empty())
      return script; // unknown table -> no-op
   std::string result = script;
   for (size_t i = 0; i < count; ++i) {
      const TablePatch& p = patches[i];
      if (ToLower(p.filename) != base)
         continue;
      int n = 0;
      if (p.isRegex) {
         try {
            const std::regex re(p.find);
            const int matchCount = static_cast<int>(
               std::distance(std::sregex_iterator(result.cbegin(), result.cend(), re), std::sregex_iterator()));
            if (matchCount > 0) {
               result = std::regex_replace(result, re, std::string(p.replace));
               n = matchCount; // accurate count for the `applied` contract {description, occurrences}
            }
         } catch (const std::regex_error&) {
            // A malformed pattern in the (author-controlled) kTablePatches registry is a
            // safe no-op; the entry's own unit test is expected to catch a bad pattern.
            n = 0;
         }
      } else {
         n = ReplaceAllLiteral(result, p.find, p.replace);
      }
      if (n > 0 && applied)
         applied->push_back({ std::string(p.description), n });
   }
   return result;
}

std::string ApplyTableSpecificPatches(const std::string& script, const std::string& tableFilename,
   std::vector<std::pair<std::string, int>>* applied) {
   return ApplyTableSpecificPatches(script, tableFilename, kTablePatches, kTablePatchCount, applied);
}

} // namespace tablepatches
