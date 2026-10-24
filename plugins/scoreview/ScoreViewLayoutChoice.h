// license:GPLv3+

#pragma once

// Which ScoreView layout to show (ScoreView::Select), and the layout built when none of the
// bundled ones fits a ROM's segment displays.
//
// A bundled layout fits when every display it draws exists with the right digit count. About
// one segment ROM in six has displays no bundled layout fits (Taito do Brasil's four 6-digit
// players with single-digit credit and ball displays, Zaccaria's five 8-digit displays,
// Playmatic, LTD, early Williams, ...), which left the panel off or showing a redraw of the
// table's own digit reels. For those, Generate() lays out whatever the ROM publishes.
//
// Pure (std only) so tests/scoreview can check it against the display lists of every ROM
// in a PinMAME set.

#include <algorithm>
#include <vector>

namespace ScoreView::LayoutChoice {

struct Score
{
   unsigned int matched = 0;
   unsigned int unmatched = 0;
   float unfitted = 0.f; // 1 when the layout has the output's aspect ratio, smaller otherwise
   bool reelOnly = false; // every visual is the live EM reel image
};

// A layout starts reel-only when it has visuals; any DMD or segment visual clears it.
inline Score Start(bool hasVisuals, float outputAR, float layoutAR)
{
   Score s;
   s.reelOnly = hasVisuals;
   s.unfitted = (outputAR > layoutAR) ? (layoutAR / outputAR) : (outputAR / layoutAR);
   return s;
}

// A DMD visual wanting wantW x wantH. Synthetic segment-to-DMD sources (AlphaDMD) never
// satisfy a default-source DMD visual: the segment layouts render the real thing.
inline void AddDmd(Score& s, bool present, bool syntheticOnDefaultUri, unsigned int srcW, unsigned int srcH, int wantW, int wantH)
{
   s.reelOnly = false;
   if (!present || syntheticOnDefaultUri || srcW * static_cast<unsigned int>(wantH) != static_cast<unsigned int>(wantW) * srcH)
      s.unmatched++;
   else
      s.matched += 10; // favor DMD over alphanumeric segment displays
}

inline void AddSeg(Score& s, bool present, int srcElements, int wantElements)
{
   s.reelOnly = false;
   if (!present || srcElements != wantElements)
      s.unmatched++;
   else
      s.matched++;
}

// The live EM reel image is the lowest priority source.
inline void AddImage(Score& s, bool reelImage)
{
   if (reelImage)
      s.matched += 1;
   else
      s.unmatched++;
}

// The layout to show, or -1 for none: the fewest unmatched visuals, then the most matched,
// then the best aspect fit. A reel-only winner gives way to any fully matched display layout,
// and a winner with unmatched visuals is not shown.
inline int Pick(const std::vector<Score>& scores)
{
   if (scores.empty())
      return -1;
   int best = static_cast<int>(std::max_element(scores.begin(), scores.end(),
      [](const Score& a, const Score& b) -> bool
      {
         if (a.unmatched != b.unmatched)
            return a.unmatched > b.unmatched;
         if (a.matched != b.matched)
            return a.matched < b.matched;
         if (a.unfitted != b.unfitted)
            return a.unfitted < b.unfitted;
         return false;
      }) - scores.begin());
   if (scores[best].reelOnly)
   {
      int real = -1;
      for (int i = 0; i < static_cast<int>(scores.size()); i++)
      {
         const Score& l = scores[i];
         if (!l.reelOnly && l.unmatched == 0
            && (real < 0 || l.matched > scores[real].matched || (l.matched == scores[real].matched && l.unfitted < scores[real].unfitted)))
            real = i;
      }
      if (real >= 0)
         best = real;
   }
   return scores[best].unmatched > 0 ? -1 : best;
}

// Whether to build a layout from the segment displays: no DMD or segment layout is shown
// (the panel is off, or shows the reel image), and the controller publishes segment displays
// but no DMD of its own.
inline bool NeedsGenerated(const std::vector<Score>& scores, int picked, size_t segDisplays, bool realDmd)
{
   if (segDisplays == 0 || realDmd)
      return false;
   return picked < 0 || scores[picked].reelOnly;
}

struct Segment
{
   int srcId;     // ctrl://default/seg?id=srcId
   int nElements;
   float x, y, w, h; // layout coordinates, y down
   std::vector<float> xOffsets;
};

struct Generated
{
   float width = 0.f, height = 0.f;
   std::vector<Segment> segments;
};

// Element geometry of the bundled layouts: a 24x32 element every 18 units for a score
// display (alpha4x7), the same proportions at height 20 for credit/ball displays.
constexpr float kScoreH = 32.f, kStatusH = 20.f, kPitch = 18.f / 32.f, kElement = 24.f / 32.f;
constexpr float kMargin = 6.f, kColumnGap = 24.f, kRowGap = 4.f, kStatusGap = 16.f;

inline float DisplayWidth(int nElements, float h) { return (nElements - 1) * kPitch * h + kElement * h; }

// Player displays (5 or more digits, or the largest when none has 5) two per row in PinMAME
// order, player 1 and 2 on the first row, a pair only when both have the same digit count;
// the shorter credit/ball/match displays smaller, in rows below. Rows are centered.
inline Generated Generate(const std::vector<int>& nElements)
{
   Generated g;
   const int largest = nElements.empty() ? 0 : *std::max_element(nElements.begin(), nElements.end());
   std::vector<int> players, status;
   for (int i = 0; i < static_cast<int>(nElements.size()); i++)
      if (nElements[i] > 0)
         ((nElements[i] >= 5 || nElements[i] == largest) ? players : status).push_back(i);

   struct Row { std::vector<int> ids; float h, gap, width; };
   std::vector<Row> rows;
   const auto rowWidth = [&nElements](const Row& r)
   {
      float w = 0.f;
      for (size_t i = 0; i < r.ids.size(); i++)
         w += (i ? r.gap : 0.f) + DisplayWidth(nElements[r.ids[i]], r.h);
      return w;
   };
   for (size_t k = 0; k < players.size();)
   {
      Row r { { players[k] }, kScoreH, kColumnGap, 0.f };
      if (k + 1 < players.size() && nElements[players[k + 1]] == nElements[players[k]])
         r.ids.push_back(players[k + 1]);
      k += r.ids.size();
      r.width = rowWidth(r);
      rows.push_back(r);
   }
   float playersW = 0.f;
   for (const Row& r : rows)
      playersW = std::max(playersW, r.width);
   // Status rows wrap at the players' width, or at least the width of two 7-digit players
   const float wrap = std::max(playersW, 2.f * DisplayWidth(7, kScoreH) + kColumnGap);
   for (size_t k = 0; k < status.size();)
   {
      Row r { {}, kStatusH, kStatusGap, 0.f };
      while (k < status.size())
      {
         r.ids.push_back(status[k]);
         if (r.ids.size() > 1 && rowWidth(r) > wrap)
         {
            r.ids.pop_back();
            break;
         }
         k++;
      }
      r.width = rowWidth(r);
      rows.push_back(r);
   }

   float contentW = 0.f;
   for (const Row& r : rows)
      contentW = std::max(contentW, r.width);
   float y = kMargin;
   for (const Row& r : rows)
   {
      float x = kMargin + (contentW - r.width) * 0.5f;
      for (const int id : r.ids)
      {
         Segment s { id, nElements[id], x, y, DisplayWidth(nElements[id], r.h), r.h, {} };
         for (int e = 0; e < s.nElements; e++)
            s.xOffsets.push_back(e * kPitch * r.h);
         x += s.w + r.gap;
         g.segments.push_back(s);
      }
      y += r.h + kRowGap;
   }
   g.width = contentW + 2.f * kMargin;
   g.height = (rows.empty() ? kMargin : y - kRowGap) + kMargin;
   return g;
}

}
