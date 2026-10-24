// license:GPLv3+

#pragma once

// Default of a table's F12 option on a phone (PinTable::RegisterOption).
//
// Some tables let the player pick where their DMD goes and default to a desktop one:
// JP's Foo Fighters, `Table1.Option("DMD Type", 0, 1, 1, 0, 0, Array("Desktop DMD", "FlexDMD"))`,
// draws its "Desktop DMD" with flashers 600 units left of the playfield. The phone frames
// the playfield, so that display is off screen, and the ScoreView only shows FlexDMD.
// When an option offers "FlexDMD" and defaults to a "Desktop ..." choice, the phone defaults
// to FlexDMD instead; a value the player picked still wins. Same policy as
// PinTable::get_ShowDT on Android: tables are told they run in a cabinet, not a desktop.
//
// Header-only, std-only: tests/test-tableoptiondefault.cpp checks it on the host.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace TableOptionDefault {

inline std::string Normalized(const std::string& s)
{
   const size_t b = s.find_first_not_of(" \t");
   if (b == std::string::npos)
      return std::string();
   const size_t e = s.find_last_not_of(" \t");
   std::string out = s.substr(b, e - b + 1);
   std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return out;
}

// literals[i] is the choice for value minValue + i. Returns the default to use on a phone.
inline int ForPhone(const std::vector<std::string>& literals, int minValue, int defaultValue)
{
   const int index = defaultValue - minValue;
   if (index < 0 || index >= static_cast<int>(literals.size()))
      return defaultValue;
   if (Normalized(literals[index]).find("desktop") == std::string::npos)
      return defaultValue;
   for (size_t i = 0; i < literals.size(); i++)
      if (Normalized(literals[i]) == "flexdmd")
         return minValue + static_cast<int>(i);
   return defaultValue;
}

}
