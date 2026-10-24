// license:GPLv3+

#include "TournamentLock.h"

namespace TournamentLock
{
   bool Allows(Role role)
   {
      switch (role)
      {
      case Role::BuyIn:
      case Role::ExtraBall:
      case Role::Reset:
      case Role::CoinDoor:
      case Role::Service:
      case Role::SlamTilt:
      case Role::Exit:
         return false;
      case Role::Start:
      case Role::Credit:
      case Role::Other:
      default:
         return true;
      }
   }

   bool Counted(Role role)
   {
      return role == Role::Start || role == Role::Credit;
   }
}
