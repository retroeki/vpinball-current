// license:GPLv3+

#pragma once

#include <cstdint>

// Tournament games (Pocket Pinball tournaments, part 4, 3.7): no buy-ins, no
// operator changes, no reset. Dependency-free so tests/test-tournament-lock.cpp
// builds it with plain g++ (the ReelClassifier pattern).
//
// Start and credit are never blocked. "One game, one player" is checked after
// the game instead: the engine counts the Start presses it delivered and the
// server sends a game with more than one to review. Blocking them once the
// game "is underway" needs a signal no table type shares: a moving ball (Cirqus
// Voltaire's script moves a hidden Ringmaster ball from load, so the lock ate
// every credit), the ROM's game-on output (original tables have no ROM),
// flipper contact.
namespace TournamentLock
{
   // What an input does, as far as the lock cares.
   enum class Role : uint8_t
   {
      Other,     // flippers, nudge, plunger, launch, magna, lockbar, menu, volume, ...
      Start,     // also the EM "1" start key; delivered and counted
      Credit,    // Credit1-4; delivered and counted
      BuyIn,     // EB buy-in / front door ("2")
      ExtraBall,
      Reset,     // F3, VPinballResetTable
      CoinDoor,  // End
      Service,   // Service 1-8 (7, 8, 9, 0, 6, PageUp, Minus)
      SlamTilt,  // Home
      Exit,      // Q, ESC: a tournament game is left through the app menu
   };

   // Deliver a press of this role to the table?
   bool Allows(Role role);

   // Presses of this role are counted for the submission (Start and credit).
   bool Counted(Role role);
}
