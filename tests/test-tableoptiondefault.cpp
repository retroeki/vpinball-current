// license:GPLv3+
// TableOptionDefault::ForPhone (src/core/TableOptionDefault.h).
//   g++ -std=c++17 -Isrc tests/test-tableoptiondefault.cpp -o /tmp/tod && /tmp/tod
// With a corpus (tests/tableoptions/extract_options.py output) and the expected list, it
// also checks that the rule changes the default of exactly the listed options:
//   /tmp/tod options.tsv tests/tableoptions/expected_changes.txt
#include "core/TableOptionDefault.h"
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using TableOptionDefault::ForPhone;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS  %s\n", msg); else { printf("FAIL  %s\n", msg); ++g_fail; } } while (0)

static int Corpus(const char* tsvPath, const char* expectedPath)
{
   std::set<std::string> expected, seen;
   std::ifstream exp(expectedPath);
   for (std::string line; std::getline(exp, line);)
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      if (!line.empty() && line[0] != '#')
         expected.insert(line);
   }
   std::ifstream in(tsvPath);
   int options = 0;
   for (std::string line; std::getline(in, line);)
   {
      if (!line.empty() && line.back() == '\r')
         line.pop_back();
      std::vector<std::string> cols;
      std::stringstream ss(line);
      for (std::string col; std::getline(ss, col, '\t');)
         cols.push_back(col);
      if (cols.size() != 5)
         continue;
      std::vector<std::string> literals;
      std::stringstream ls(cols[4]);
      for (std::string lit; std::getline(ls, lit, '\x1f');)
         literals.push_back(lit);
      options++;
      const int lo = std::stoi(cols[2]), def = std::stoi(cols[3]);
      const int phone = ForPhone(literals, lo, def);
      if (phone != def)
      {
         const std::string key = cols[0] + '\t' + cols[1];
         seen.insert(key);
         printf("%s  %s | %s: %s -> %s\n", expected.count(key) ? "changed   " : "UNEXPECTED", cols[0].c_str(), cols[1].c_str(),
            literals[def - lo].c_str(), literals[phone - lo].c_str());
      }
   }
   int bad = 0;
   for (const std::string& key : seen)
      bad += expected.count(key) ? 0 : 1;
   for (const std::string& key : expected)
      if (!seen.count(key))
      {
         printf("MISSING     %s\n", key.c_str());
         bad++;
      }
   printf("%d options with choices, %zu defaults changed, %d unexpected or missing\n", options, seen.size(), bad);
   return bad ? 1 : 0;
}

int main(int argc, char** argv)
{
   // JP's Foo Fighters: Table1.Option("DMD Type", 0, 1, 1, 0, 0, Array("Desktop DMD", "FlexDMD"))
   CHECK(ForPhone({ "Desktop DMD", "FlexDMD" }, 0, 0) == 1, "Foo Fighters: Desktop DMD default -> FlexDMD");
   CHECK(ForPhone({ "Desktop DMD", "FlexDMD" }, 0, 1) == 1, "FlexDMD default stays");
   CHECK(ForPhone({ "desktop", " flexdmd " }, 0, 0) == 1, "case and padding ignored");
   CHECK(ForPhone({ "Desktop DMD", "FlexDMD", "PuP" }, 1, 1) == 2, "min value 1: index offset kept");
   CHECK(ForPhone({ "FlexDMD", "PuPDMD" }, 0, 1) == 1, "PuP default (not a desktop display) stays");
   CHECK(ForPhone({ "Desktop DMD", "B2S" }, 0, 0) == 0, "no FlexDMD choice -> unchanged");
   CHECK(ForPhone({ "Hide", "Show" }, 0, 1) == 1, "Iron Man 2's 'Desktop DMD' Hide/Show option -> unchanged");
   CHECK(ForPhone({ "Desktop DMD", "FlexDMD" }, 0, 5) == 5, "default out of range -> unchanged");
   CHECK(ForPhone({}, 0, 0) == 0, "no choices -> unchanged");
   int status = g_fail ? 1 : 0;
   printf("%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
   if (argc > 2)
      status |= Corpus(argv[1], argv[2]);
   return status;
}
