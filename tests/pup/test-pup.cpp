// license:GPLv3+
// Host tests for the PuP plugin's Android additions, against config files copied from real
// packs (tests/pup/fixtures, see README.md):
//   g++ -std=c++20 -Iplugins/pup tests/pup/test-pup.cpp -o /tmp/test_pup && /tmp/test_pup tests/pup/fixtures
#include "PUPScreenType.h"
#include "PUPScoreTracker.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace PUP;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS  %s\n", msg); else { printf("FAIL  %s\n", msg); ++g_fail; } } while (0)

static std::string g_fixtures;

// The path a table builds: PuPlayer.GetRoot & "<pack>\ScreenType.txt".
static std::string Resolve(const std::string& variant, const std::string& pack, std::string* how = nullptr)
{
   return ScreenType::Resolve(g_fixtures + "/" + variant + "/pupvideos/" + pack + "\\ScreenType.txt", how);
}

static void TestScreenType()
{
   std::string how;
   // Darkest Dungeon 3.06 zip: ScreenType.txt says 99 (no layout chosen); its screens.pup is the
   // FullDMD layout (also Desktop and VR), whose .bat files all write 2.
   CHECK(Resolve("default", "DarkestDungeon", &how) == "2", "Darkest Dungeon zip: 99 in the file, 2 from the FullDMD layout");
   // ... after running "3 Screen - Slim DMD.bat" on Windows: the file and the layout agree.
   CHECK(Resolve("dd_slim", "DarkestDungeon") == "1 ", "Darkest Dungeon Slim DMD: the file's 1 (as ReadLine returns it)");
   // Terrifier zip: file 2, screens.pup is the Desktop-VR layout whose .bat writes 2.
   CHECK(Resolve("default", "TERRIFIER") == "2 ", "Terrifier zip: the file's 2 matches the layout");
   CHECK(Resolve("default", "terrifier") == "2 ", "Terrifier: pack folder found ignoring case");
   // BTILC: its screens.pup matches two layouts (Desktop writes 0, FullDMD writes 3); the file picks 0.
   CHECK(Resolve("default", "BTILC") == "0 ", "BTILC: the file's 0 is one of the matching layouts");
   CHECK(Resolve("btilc_unset", "BTILC") == "0", "BTILC without ScreenType.txt: first matching layout .bat (1 - Desktop.bat)");
   // b66 Original: layout .bat files but none writes ScreenType.txt, and there is no file.
   CHECK(Resolve("default", "b66_orig") == ScreenType::kFallback, "b66: no ScreenType anywhere -> fallback");
   // Harry Potter: one layout, no options, no file.
   CHECK(Resolve("default", "hpgof") == ScreenType::kFallback, "hpgof: no options and no file -> fallback");
   // No pack: GetRoot is "" on the device, so the table asks for a relative path.
   CHECK(ScreenType::Resolve("NoSuchPack\\ScreenType.txt") == ScreenType::kFallback, "no pack (GetRoot empty) -> fallback");
   CHECK(Resolve("default", "NoSuchPack") == ScreenType::kFallback, "pack folder missing -> fallback");

   const auto bat = ScreenType::ParseBat("4.bat", "rem echo off\r\nxcopy /y \"PuP-Pack_Options\\4 Screen - BG DMD Topper Separate\\*.pup\"\r\ndel pupinit.bat\r\necho 2 > ScreenType.txt\"");
   CHECK(bat && bat->optionFolder == "4 Screen - BG DMD Topper Separate" && bat->value == "2", "ParseBat: stray quote after ScreenType.txt");
   CHECK(!ScreenType::ParseBat("6.bat", "rem echo off\r\nxcopy /y \"PuP-Pack_Options\\Pinup-1.5\\*\""), "ParseBat: copies a layout but writes no ScreenType -> not a layout");
   CHECK(!ScreenType::ParseBat("x.bat", "@echo off\r\nstart PuPDesktopPunch.exe \"Visual Pinball Player\" 10"), "ParseBat: unrelated .bat");
}

struct Set { int root, screen; const char* label; std::string caption; bool visible; };

// Feeds LabelSets until the tracker decides; returns its answer or -1.
static int Run(const std::vector<Set>& sets, int currentRoot)
{
   ScoreTracker tracker;
   for (const Set& s : sets)
   {
      const int r = tracker.OnLabelSet(s.root, s.screen, s.label, s.caption, s.visible, currentRoot);
      if (r >= 0)
         return r;
   }
   return -1;
}

static std::string Num(long long v)
{
   std::string s = std::to_string(v), out;
   for (size_t i = 0; i < s.size(); i++)
   {
      if (i && (s.size() - i) % 3 == 0)
         out += ',';
      out += s[i];
   }
   return out;
}

static void TestScoreTracker()
{
   CHECK(ScoreTracker::ParseScore("1,234,567") == 1234567, "ParseScore: commas");
   CHECK(ScoreTracker::ParseScore(" 12.345 ") == 12345, "ParseScore: dots and padding");
   CHECK(ScoreTracker::ParseScore("00") == 0, "ParseScore: Darkest Dungeon's zero");
   CHECK(!ScoreTracker::ParseScore("Ball 2"), "ParseScore: text");
   CHECK(!ScoreTracker::ParseScore("x"), "ParseScore: Harry Potter's placeholder");
   CHECK(!ScoreTracker::ParseScore(""), "ParseScore: empty");
   CHECK(!ScoreTracker::ParseScore("1,000,"), "ParseScore: trailing separator");

   // Harry Potter: the score is on the backglass (2); screen 1 (DMD, the first guess) only
   // gets text pages. Moves to 2.
   {
      std::vector<Set> s = { { 2, 2, "curscore", "x", true }, { 1, 1, "Splash", "WELCOME", true } };
      for (long long v = 0; v <= 40000; v += 5000)
         s.push_back({ 2, 2, "curscore", Num(v), true });
      CHECK(Run(s, 1) == 2, "Harry Potter: score on the backglass -> ScoreView moves to 2");
   }
   // Standard PuPDMD table: the score is on screen 1, the backglass shows high scores (numbers
   // that never go up). Stays on 1.
   {
      std::vector<Set> s;
      for (int i = 0; i < 4; i++)
         s.push_back({ 2, 2, "HS-Score", Num(1000000 * (4 - i)), true });
      for (long long v = 0; v <= 20000; v += 1000)
         s.push_back({ 1, 1, "CurScore", Num(v), true });
      CHECK(Run(s, 1) == 1, "PuPDMD table: score on screen 1 -> stays on 1");
   }
   // Darkest Dungeon: ScoreUpdate writes the score to pDMD (13, a child of 5) and to the
   // backglass; the first guess is 5. Stays on 5 whichever label is written first.
   {
      std::vector<Set> a, b;
      for (long long v = 0; v <= 50000; v += 2500)
      {
         a.push_back({ 5, 13, "CurrScore", Num(v), true });
         a.push_back({ 2, 2, "CurrScore", Num(v), true });
         b.push_back({ 2, 2, "CurrScore", Num(v), true });
         b.push_back({ 5, 13, "CurrScore", Num(v), true });
      }
      CHECK(Run(a, 5) == 5, "Darkest Dungeon: score on 13 and the backglass, DMD first -> stays on 5");
      CHECK(Run(b, 5) == 5, "Darkest Dungeon: score on 13 and the backglass, backglass first -> stays on 5");
   }
   // Terrifier with the wrong ScreenType (1): pDMD is 12, a child of the backglass, while the
   // first guess is 5. Moves to 2, so the score is still shown.
   {
      std::vector<Set> s;
      for (long long v = 0; v <= 30000; v += 3000)
         s.push_back({ 2, 12, "CurrScore", Num(v), true });
      CHECK(Run(s, 5) == 2, "Terrifier, pDMD 12: score under the backglass -> moves to 2");
   }
   // Small counters going up on another screen (spins, combos) are not the score.
   {
      std::vector<Set> s;
      for (int v = 0; v < 50; v++)
         s.push_back({ 2, 2, "lakespins", std::to_string(v), true });
      for (long long v = 0; v <= 20000; v += 1000)
         s.push_back({ 1, 1, "CurScore", Num(v), true });
      CHECK(Run(s, 1) == 1, "counter under 1000 on the backglass -> stays on 1");
   }
   // Hidden updates do not count (Darkest Dungeon hides the backglass score in guide mode).
   {
      std::vector<Set> s;
      for (long long v = 0; v <= 30000; v += 3000)
         s.push_back({ 2, 2, "CurrScore", Num(v), false });
      CHECK(Run(s, 1) == -1, "hidden score label -> no decision");
   }
   // A number that only goes down or repeats is not the score (match/bonus countdown).
   {
      std::vector<Set> s;
      for (long long v = 50000; v >= 0; v -= 5000)
         s.push_back({ 2, 2, "Bonus", Num(v), true });
      for (int i = 0; i < 10; i++)
         s.push_back({ 2, 2, "HighScore", "5,000,000", true });
      CHECK(Run(s, 1) == -1, "countdown and repeated numbers -> no decision");
   }
   // Decided once: later labels do not move it again.
   {
      ScoreTracker t;
      int first = -1;
      for (long long v = 0; v <= 5000 && first < 0; v += 1000)
         first = t.OnLabelSet(1, 1, "CurScore", Num(v), true, 1);
      int later = -1;
      for (long long v = 0; v <= 50000; v += 5000)
         later = std::max(later, t.OnLabelSet(2, 2, "curscore", Num(v), true, 1));
      CHECK(first == 1 && later == -1 && t.IsDecided(), "decided once per run");
      t.Reset();
      CHECK(!t.IsDecided(), "Reset starts over (next table)");
   }
}

int main(int argc, char** argv)
{
   g_fixtures = argc > 1 ? argv[1] : "tests/pup/fixtures";
   TestScreenType();
   TestScoreTracker();
   printf("%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
   return g_fail ? 1 : 0;
}
