// license:GPLv3+
// PuPlayer.ReadScreenType for the stand-in table (table_host.c): the plugin's own code.
#include "PUPScreenType.h"

#include <cstring>

extern "C" void pup_read_screen_type(const char* path, char* out, size_t size)
{
   const std::string value = PUP::ScreenType::Resolve(path);
   snprintf(out, size, "%s", value.c_str());
}
