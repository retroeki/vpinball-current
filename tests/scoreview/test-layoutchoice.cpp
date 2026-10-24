// license:GPLv3+
// ScoreView layout choice (plugins/scoreview/ScoreViewLayoutChoice.h) against the displays of
// every ROM of a PinMAME set (rom_displays.tsv, see README.md) and the shipped layouts:
//   g++ -std=c++20 -Iplugins/scoreview tests/scoreview/test-layoutchoice.cpp -o /tmp/tlc
//   /tmp/tlc plugins/scoreview/layouts tests/scoreview
//
// - every ROM a shipped layout fits still gets that layout (expected_layouts.tsv, worked out
//   independently of this code), with or without a reel image on the table;
// - every other segment ROM gets a generated layout holding each of its displays once, inside
//   the layout, without overlaps;
// - a table with a real DMD never gets a generated layout.
#include "ScoreViewLayoutChoice.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace ScoreView::LayoutChoice;

struct Visual { std::string type, src; int n = -1, sizeW = 0, sizeH = 0; };
struct Layout { std::string name; float w = 1920.f, h = 1080.f; std::vector<Visual> visuals; };

static int g_fail = 0;
static int g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { printf("FAIL  " __VA_ARGS__); printf("\n"); ++g_fail; } } while (0)

static std::vector<Layout> ReadLayouts(const std::string& dir)
{
   std::vector<Layout> layouts;
   std::vector<std::filesystem::path> files;
   for (const auto& e : std::filesystem::directory_iterator(dir))
      if (e.path().extension() == ".scv")
         files.push_back(e.path());
   std::sort(files.begin(), files.end());
   static const std::regex number(R"(-?[0-9.]+)");
   for (const auto& f : files)
   {
      Layout l;
      l.name = f.stem().string();
      std::ifstream in(f);
      for (std::string line; std::getline(in, line);)
      {
         const size_t b = line.find_first_not_of(" \t");
         if (b == std::string::npos || line[b] == '#')
            continue;
         std::string s = line.substr(b);
         while (!s.empty() && (s.back() == '\r' || s.back() == ' '))
            s.pop_back();
         auto numbers = [&](const std::string& v) {
            std::vector<float> out;
            for (std::sregex_iterator it(v.begin(), v.end(), number), end; it != end; ++it)
               out.push_back(std::stof(it->str()));
            return out;
         };
         if (s.rfind("Width:", 0) == 0) l.w = std::stof(s.substr(6));
         else if (s.rfind("Height:", 0) == 0) l.h = std::stof(s.substr(7));
         else if (s.rfind("- ", 0) == 0)
         {
            Visual v;
            v.type = s.substr(2, s.find(':') - 2);
            v.src = v.type == "DMD" ? "ctrl://default/display" : v.type == "Image" ? "ctrl://reel" : ""; // ScoreView::Parse defaults
            l.visuals.push_back(v);
         }
         else if (!l.visuals.empty() && s.rfind("Src:", 0) == 0) l.visuals.back().src = s.substr(s.find_first_not_of(" \t", 4));
         else if (!l.visuals.empty() && s.rfind("XPos:", 0) == 0) l.visuals.back().n = (int)numbers(s.substr(5)).size();
         else if (!l.visuals.empty() && s.rfind("NElements:", 0) == 0) l.visuals.back().n = std::stoi(s.substr(10));
         else if (!l.visuals.empty() && s.rfind("Size:", 0) == 0) { auto v = numbers(s.substr(5)); l.visuals.back().sizeW = (int)v[0]; l.visuals.back().sizeH = (int)v[1]; }
      }
      layouts.push_back(l);
   }
   return layouts;
}

struct Sources
{
   std::vector<int> segs;            // ctrl://default/seg?id=i
   int dmdW = 0, dmdH = 0;           // ctrl://default/display
   bool dmdSynthetic = false;        // AlphaDMD's render of the segment displays
   bool reelImage = false;
};

static std::vector<Score> ScoreAll(const std::vector<Layout>& layouts, const Sources& src, float outputAR)
{
   std::vector<Score> scores;
   for (const Layout& l : layouts)
   {
      Score s = Start(!l.visuals.empty(), outputAR, l.w / l.h);
      for (const Visual& v : l.visuals)
      {
         if (v.type == "DMD")
            AddDmd(s, src.dmdW > 0, src.dmdSynthetic && v.src.rfind("ctrl://default/", 0) == 0, src.dmdW, src.dmdH, v.sizeW, v.sizeH);
         else if (v.type == "SegDisplay")
         {
            std::smatch m;
            const int id = std::regex_search(v.src, m, std::regex(R"(id=(\d+))")) ? std::stoi(m[1]) : 0;
            const bool present = id < (int)src.segs.size();
            AddSeg(s, present, present ? src.segs[id] : 0, v.n);
         }
         else
            AddImage(s, src.reelImage);
      }
      scores.push_back(s);
   }
   return scores;
}

static void CheckGenerated(const std::string& rom, const std::vector<int>& segs)
{
   const Generated g = Generate(segs);
   CHECK(g.segments.size() == segs.size(), "%s: %zu of %zu displays laid out", rom.c_str(), g.segments.size(), segs.size());
   std::vector<int> seen(segs.size(), 0);
   for (const Segment& s : g.segments)
   {
      CHECK(s.srcId >= 0 && s.srcId < (int)segs.size() && s.nElements == segs[s.srcId] && (int)s.xOffsets.size() == s.nElements,
         "%s: display %d has %d elements", rom.c_str(), s.srcId, s.nElements);
      if (s.srcId >= 0 && s.srcId < (int)segs.size())
         seen[s.srcId]++;
      const float drawW = s.xOffsets.empty() ? 0.f : s.xOffsets.back() + kElement * s.h; // what Render draws
      CHECK(s.x >= 0.f && s.y >= 0.f && s.x + drawW <= g.width + 0.01f && s.y + s.h <= g.height + 0.01f,
         "%s: display %d inside %.0fx%.0f", rom.c_str(), s.srcId, g.width, g.height);
   }
   for (size_t i = 0; i < seen.size(); i++)
      CHECK(seen[i] == 1, "%s: display %zu laid out once", rom.c_str(), i);
   for (size_t i = 0; i < g.segments.size(); i++)
      for (size_t j = i + 1; j < g.segments.size(); j++)
      {
         const Segment& a = g.segments[i];
         const Segment& b = g.segments[j];
         const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
         CHECK(apart, "%s: displays %d and %d overlap", rom.c_str(), a.srcId, b.srcId);
      }
   const float ar = g.width / g.height;
   CHECK(ar >= 1.f && ar <= 16.f, "%s: generated aspect %.2f", rom.c_str(), ar);
}

int main(int argc, char** argv)
{
   const std::string layoutDir = argc > 1 ? argv[1] : "plugins/scoreview/layouts";
   const std::string dataDir = argc > 2 ? argv[2] : "tests/scoreview";
   const std::vector<Layout> layouts = ReadLayouts(layoutDir);
   int reelLayout = -1;
   for (int i = 0; i < (int)layouts.size(); i++)
      if (layouts[i].name == "lay-reels")
         reelLayout = i;
   CHECK(!layouts.empty() && reelLayout >= 0, "layouts loaded from %s", layoutDir.c_str());

   std::map<std::string, std::string> expected;
   {
      std::ifstream in(dataDir + "/expected_layouts.tsv");
      for (std::string line; std::getline(in, line);)
      {
         if (!line.empty() && line.back() == '\r') // CRLF checkouts
            line.pop_back();
         if (line.empty() || line[0] == '#')
            continue;
         const size_t tab = line.find('\t');
         expected[line.substr(0, tab)] = line.substr(tab + 1);
      }
   }

   int roms = 0, kept = 0, generated = 0;
   std::map<std::string, int> generatedBySignature;
   std::ifstream in(dataDir + "/rom_displays.tsv");
   for (std::string line; std::getline(in, line);)
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      if (line.empty() || line[0] == '#')
         continue;
      std::vector<std::string> cols;
      std::stringstream ss(line);
      for (std::string c; std::getline(ss, c, '\t');)
         cols.push_back(c);
      while (cols.size() < 5)
         cols.push_back("");
      Sources src;
      std::stringstream segs(cols[3]);
      for (std::string n; std::getline(segs, n, ',');)
         src.segs.push_back(std::stoi(n));
      if (src.segs.empty())
         continue;
      src.dmdW = 128; src.dmdH = 32; src.dmdSynthetic = true; // AlphaDMD renders the segments
      const std::string& rom = cols[0];
      const std::string want = expected.count(rom) ? expected[rom] : "?";
      roms++;
      for (const bool reel : { false, true }) // with and without the table's own digit reels redrawn
      {
         src.reelImage = reel;
         for (const float outputAR : { 4.f, 2.f }) // landscape and portrait panel shapes
         {
            const std::vector<Score> scores = ScoreAll(layouts, src, outputAR);
            const int picked = Pick(scores);
            const bool gen = NeedsGenerated(scores, picked, src.segs.size(), false);
            if (want != "NONE")
            {
               CHECK(picked >= 0 && layouts[picked].name == want && !gen, "%s (reel=%d): kept %s, got %s%s", rom.c_str(), reel, want.c_str(),
                  picked >= 0 ? layouts[picked].name.c_str() : "none", gen ? " + generated" : "");
            }
            else
            {
               CHECK(gen && (reel ? picked == reelLayout : picked < 0), "%s (reel=%d): no layout fits, generated", rom.c_str(), reel);
            }
         }
      }
      if (want != "NONE")
         kept++;
      else
      {
         generated++;
         generatedBySignature[cols[3]]++;
         CheckGenerated(rom, src.segs);
      }
   }

   // A real DMD keeps its layout and never gets a generated one, even with segment displays
   {
      Sources dmd;
      dmd.dmdW = 128; dmd.dmdH = 32;
      std::vector<Score> scores = ScoreAll(layouts, dmd, 4.f);
      int picked = Pick(scores);
      CHECK(picked >= 0 && layouts[picked].name == "lay4x1-dmd128x32" && !NeedsGenerated(scores, picked, 0, true), "128x32 DMD keeps its layout");
      dmd.dmdW = 256; dmd.dmdH = 64; // FlexDMD high quality
      scores = ScoreAll(layouts, dmd, 4.f);
      picked = Pick(scores);
      CHECK(picked >= 0 && layouts[picked].name == "lay4x1-dmd128x32", "256x64 DMD uses the 4:1 layout");
      dmd.dmdW = 160; dmd.dmdH = 32; dmd.segs = { 6, 6, 1, 1 }; // a DMD no layout fits, with segment displays
      scores = ScoreAll(layouts, dmd, 4.f);
      picked = Pick(scores);
      CHECK(picked < 0 && !NeedsGenerated(scores, picked, dmd.segs.size(), true), "unfitted real DMD: no generated layout");
      Sources em; // EM table: only the reel image
      em.reelImage = true;
      scores = ScoreAll(layouts, em, 4.f);
      picked = Pick(scores);
      CHECK(picked == reelLayout && !NeedsGenerated(scores, picked, 0, false), "EM table keeps the reel image");
      Sources none; // no sources at all
      scores = ScoreAll(layouts, none, 4.f);
      CHECK(Pick(scores) < 0 && !NeedsGenerated(scores, Pick(scores), 0, false), "no sources: panel off");
   }

   printf("%d segment ROMs: %d keep their layout, %d get a generated one (%d checks)\n", roms, kept, generated, g_checks);
   for (const auto& [sig, n] : generatedBySignature)
      if (n >= 3)
         printf("   %4d  generated for [%s]\n", n, sig.c_str());
   printf("%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
   return g_fail ? 1 : 0;
}
