// license:GPLv3+
// Build & run from the repo root:
//   g++ -std=c++17 -I src tests/test-tournament-lock.cpp src/core/TournamentLock.cpp -o tltest && ./tltest
// Links only TournamentLock.cpp; no engine, runs anywhere a C++17 compiler is available.
#include "core/TournamentLock.h"
#include <cstdio>

using namespace TournamentLock;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS  %s\n", msg); else { printf("FAIL  %s\n", msg); ++g_fail; } } while (0)

int main()
{
   // Part 4, 3.7 decision table, as amended 2026-10-11 (Start and credit are
   // delivered and counted, never blocked).
   CHECK(Allows(Role::Start), "start always delivered");
   CHECK(Allows(Role::Credit), "credit always delivered");
   CHECK(Counted(Role::Start), "start presses counted");
   CHECK(Counted(Role::Credit), "credit presses counted");
   const Role always[] = { Role::BuyIn, Role::ExtraBall, Role::Reset, Role::CoinDoor, Role::Service, Role::SlamTilt, Role::Exit };
   for (Role r : always)
   {
      CHECK(!Allows(r), "buy-in, extra ball, reset, coin door, service, slam tilt and exit blocked");
      CHECK(!Counted(r), "blocked roles not counted");
   }
   CHECK(Allows(Role::Other), "flippers, nudge, plunger always allowed");
   CHECK(!Counted(Role::Other), "flippers, nudge, plunger not counted");
   printf("%s (%d failures)\n", g_fail ? "FAILURES" : "ALL PASS", g_fail);
   return g_fail ? 1 : 0;
}
