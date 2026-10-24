// license:GPLv3+

#pragma once

// ScreenType.txt for PuP packs that ship several screen layouts (Darkest Dungeon, Terrifier,
// BTILC, Blizzard of Ozz, Thundercats). On Windows the user runs one of the pack's layout
// .bat files: it copies PuP-Pack_Options\<layout>\*.pup over the pack's own files and writes
// a number to ScreenType.txt, which the table reads to decide which screen gets its labels.
// Nobody runs a .bat on the device, so the file holds whatever the zip shipped ("99" in
// Darkest Dungeon 3.06) or does not exist. Resolve() works the number out from the layout
// the pack is actually set to: the .bat whose option folder holds the same screens.pup.
//
// Header-only and free of plugin dependencies so tests/pup can check it against real packs.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace PUP::ScreenType {

// What the patched scripts used before the pack could be read: keeps tables without a PuP
// pack starting (their GetRoot is "" and the file does not exist).
inline const std::string kFallback = "1";

inline std::string Lower(std::string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return s;
}

inline std::string Trim(const std::string& s)
{
   const size_t b = s.find_first_not_of(" \t\r\n");
   if (b == std::string::npos)
      return std::string();
   const size_t e = s.find_last_not_of(" \t\r\n");
   return s.substr(b, e - b + 1);
}

inline std::optional<std::string> ReadFile(const std::filesystem::path& path)
{
   std::ifstream in(path, std::ios::binary);
   if (!in)
      return std::nullopt;
   std::ostringstream ss;
   ss << in.rdbuf();
   return ss.str();
}

// Entry of dir whose name matches name ignoring case (the device file system is case
// sensitive; pack folders and table scripts do not agree on case).
inline std::filesystem::path FindNoCase(const std::filesystem::path& dir, const std::string& name)
{
   std::error_code ec;
   if (std::filesystem::exists(dir / name, ec))
      return dir / name;
   const std::string want = Lower(name);
   for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
      if (Lower(it->path().filename().string()) == want)
         return it->path();
   return std::filesystem::path();
}

// screens.pup content compared line by line, ignoring line endings, trailing blanks and case.
inline std::string NormalizedScreens(const std::string& text)
{
   std::string out, line;
   std::istringstream in(text);
   while (std::getline(in, line))
   {
      line = Lower(Trim(line));
      if (!line.empty())
         out += line + '\n';
   }
   return out;
}

struct Layout
{
   std::string bat;          // file name of the .bat
   std::string optionFolder; // PuP-Pack_Options sub folder it copies
   std::string value;        // what it writes to ScreenType.txt
};

// A layout .bat: `xcopy /y "PuP-Pack_Options\<folder>\*.pup"` and `echo <n> > ScreenType.txt`.
inline std::optional<Layout> ParseBat(const std::string& batName, const std::string& text)
{
   static const std::regex xcopy(R"(xcopy\s+(?:/\w+\s+)*"?PuP-Pack_Options[\\/]([^\\/"]+)[\\/])", std::regex::icase);
   static const std::regex echo(R"(echo\s+(\S+)\s*>\s*"?ScreenType\.txt"?)", std::regex::icase);
   Layout layout { batName, std::string(), std::string() };
   std::string line;
   std::istringstream in(text);
   while (std::getline(in, line))
   {
      std::smatch m;
      if (layout.optionFolder.empty() && std::regex_search(line, m, xcopy))
         layout.optionFolder = Trim(m[1].str());
      else if (layout.value.empty() && std::regex_search(line, m, echo))
         layout.value = m[1].str();
   }
   if (layout.optionFolder.empty() || layout.value.empty())
      return std::nullopt;
   return layout;
}

// Values written by the .bat files whose option folder has the pack's current screens.pup,
// in .bat file name order.
inline std::vector<std::string> CandidateValues(const std::filesystem::path& packDir)
{
   std::vector<std::string> values;
   const std::filesystem::path rootScreens = FindNoCase(packDir, "screens.pup");
   const std::filesystem::path options = FindNoCase(packDir, "PuP-Pack_Options");
   if (rootScreens.empty() || options.empty())
      return values;
   const std::optional<std::string> root = ReadFile(rootScreens);
   if (!root)
      return values;
   const std::string current = NormalizedScreens(*root);

   std::vector<std::filesystem::path> bats;
   std::error_code ec;
   for (std::filesystem::directory_iterator it(packDir, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == ".bat")
         bats.push_back(it->path());
   std::sort(bats.begin(), bats.end());

   for (const std::filesystem::path& bat : bats)
   {
      const std::optional<std::string> text = ReadFile(bat);
      if (!text)
         continue;
      const std::optional<Layout> layout = ParseBat(bat.filename().string(), *text);
      if (!layout)
         continue;
      const std::filesystem::path folder = FindNoCase(options, layout->optionFolder);
      if (folder.empty())
         continue;
      const std::filesystem::path screens = FindNoCase(folder, "screens.pup");
      if (screens.empty())
         continue;
      const std::optional<std::string> option = ReadFile(screens);
      if (option && NormalizedScreens(*option) == current)
         values.push_back(layout->value);
   }
   return values;
}

// What `DMDType = ObjFile.ReadLine` should give for the ScreenType.txt path the table built
// (PuPlayer.GetRoot & "<pack>\ScreenType.txt"):
// - the file's first line when it names one of the layouts matching the pack's screens.pup
//   (the user ran that .bat, or copied that layout by hand);
// - else the value of the first matching layout .bat;
// - else the file's first line, as ReadLine would return it;
// - else kFallback (no pack, or no file and no layout .bat).
inline std::string Resolve(const std::string& screenTypePath, std::string* how = nullptr)
{
   std::string path = screenTypePath;
   std::replace(path.begin(), path.end(), '\\', '/');
   const std::filesystem::path given(path);
   const std::filesystem::path root = given.parent_path().parent_path();
   const std::string packName = given.parent_path().filename().string();

   std::filesystem::path packDir;
   if (!packName.empty())
      packDir = FindNoCase(root.empty() ? std::filesystem::path(".") : root, packName);

   std::optional<std::string> fileLine;
   if (!packDir.empty())
   {
      const std::filesystem::path file = FindNoCase(packDir, given.filename().string());
      if (!file.empty())
         if (const std::optional<std::string> text = ReadFile(file))
            fileLine = text->substr(0, text->find_first_of("\r\n"));
   }

   const std::vector<std::string> candidates = packDir.empty() ? std::vector<std::string>() : CandidateValues(packDir);
   if (fileLine && std::find(candidates.begin(), candidates.end(), Trim(*fileLine)) != candidates.end())
   {
      if (how) *how = "ScreenType.txt, matches the pack's layout";
      return *fileLine;
   }
   if (!candidates.empty())
   {
      if (how) *how = "layout .bat matching the pack's screens.pup";
      return candidates.front();
   }
   if (fileLine)
   {
      if (how) *how = "ScreenType.txt (no layout .bat matches)";
      return *fileLine;
   }
   if (how) *how = "fallback (no PuP pack or ScreenType.txt)";
   return kFallback;
}

}
