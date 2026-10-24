// license:GPLv3+

#pragma once

// Finds the PuP screen that shows the player's score, for the Android ScoreView, which shows
// a single PuP screen (with its child screens). PUPManager::DetermineScoreViewScreen guesses
// from screens.pup before any label exists; packs disagree on where the score goes (Harry
// Potter draws it on the backglass while screen 1 plays its own video), so the guess is
// checked against the labels the table actually updates.
//
// The score label is the first visible label whose caption is a number that has gone up
// kIncreases times and reached kMinScore. If the screen already shown has such a number
// too (Darkest Dungeon writes the score to both screens), it stays; otherwise the ScoreView
// moves to the top-level screen holding the score label. Decided once per table run.
//
// Header-only and free of plugin dependencies so tests/pup can check it.

#include <cctype>
#include <map>
#include <optional>
#include <string>

namespace PUP {

class ScoreTracker final
{
public:
   static constexpr int kIncreases = 3;
   static constexpr long long kMinScore = 1000;

   void Reset()
   {
      m_labels.clear();
      m_decided = false;
   }

   bool IsDecided() const { return m_decided; }

   // A LabelSet on screenNum, whose top-level screen is rootScreen. Returns the top-level
   // screen the ScoreView should show once the score label is found (currentRoot when it
   // already shows the score), else -1.
   int OnLabelSet(int rootScreen, int screenNum, const std::string& label, const std::string& caption, bool visible, int currentRoot)
   {
      if (m_decided)
         return -1;
      State& state = m_labels[std::to_string(screenNum) + '\n' + Lower(label)];
      state.root = rootScreen;
      const std::optional<long long> value = visible ? ParseScore(caption) : std::nullopt;
      state.shown = value.has_value();
      if (!value)
         return -1;
      if (state.value && *value > *state.value)
         state.increases++;
      state.value = value;
      if (!IsScore(state))
         return -1;

      m_decided = true;
      if (rootScreen == currentRoot)
         return currentRoot;
      for (const auto& [key, other] : m_labels)
         if (other.root == currentRoot && other.shown && other.value && other.increases >= kIncreases - 1 && *other.value >= kMinScore)
            return currentRoot;
      return rootScreen;
   }

   // "12,345,678", "12.345.678", "12 345", "00": digits with thousands separators only.
   static std::optional<long long> ParseScore(const std::string& caption)
   {
      size_t b = caption.find_first_not_of(" \t");
      if (b == std::string::npos)
         return std::nullopt;
      const size_t e = caption.find_last_not_of(" \t");
      long long value = 0;
      int digits = 0;
      for (size_t i = b; i <= e; i++)
      {
         const unsigned char c = static_cast<unsigned char>(caption[i]);
         if (std::isdigit(c))
         {
            if (++digits > 18)
               return std::nullopt;
            value = value * 10 + (c - '0');
         }
         else if (c == ',' || c == '.' || c == '\'' || c == ' ')
            continue;
         else if (c == 0xC2 && i + 1 <= e && static_cast<unsigned char>(caption[i + 1]) == 0xA0) // UTF-8 no-break space
            i++;
         else
            return std::nullopt;
      }
      if (digits == 0 || !std::isdigit(static_cast<unsigned char>(caption[b])) || !std::isdigit(static_cast<unsigned char>(caption[e])))
         return std::nullopt;
      return value;
   }

private:
   struct State
   {
      int root = -1;
      std::optional<long long> value;
      int increases = 0;
      bool shown = false;
   };

   static bool IsScore(const State& state) { return state.increases >= kIncreases && state.value && *state.value >= kMinScore; }

   static std::string Lower(std::string s)
   {
      for (char& c : s)
         c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
   }

   std::map<std::string, State> m_labels;
   bool m_decided = false;
};

}
