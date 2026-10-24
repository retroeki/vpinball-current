// license:GPLv3+
// Prints a ROM's display layout as PinMAME reports it (OnDisplayAvailable), then exits.
// Source of tests/scoreview/rom_displays.tsv (see README.md):
//   probe_displays VPM_PATH ROM      (VPM_PATH holds roms/<ROM>.zip)
#include "libpinmame.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unistd.h>

static const char* g_rom;

static void PINMAMECALLBACK OnGame(PinmameGame* game, void*)
{
   printf("GAME\t%s\t%s\t%s\t%s\n", g_rom, game->manufacturer, game->year, game->description);
}

static void PINMAMECALLBACK OnDisplayAvailable(int index, int count, PinmameDisplayLayout* l, void*)
{
   printf("DISPLAY\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", g_rom, index, count, (int)l->type, l->top, l->left, l->length, l->width, l->height);
   if (index == count - 1)
   {
      printf("DONE\t%s\n", g_rom);
      fflush(stdout);
      _exit(0);
   }
}

static void PINMAMECALLBACK OnState(int, void*) {}
static void PINMAMECALLBACK OnDisplayUpdated(int, void*, PinmameDisplayLayout*, void*) {}
static int PINMAMECALLBACK OnAudioAvailable(PinmameAudioInfo*, void*) { return 0; }
static int PINMAMECALLBACK OnAudioUpdated(void*, int samples, void*) { return samples; }
static void PINMAMECALLBACK OnMech(int, PinmameMechInfo*, void*) {}
static void PINMAMECALLBACK OnSol(PinmameSolenoidState*, void*) {}
static void PINMAMECALLBACK OnConsole(void*, int, void*) {}
static int PINMAMECALLBACK IsKey(PINMAME_KEYCODE, void*) { return 0; }
static void PINMAMECALLBACK OnLog(PINMAME_LOG_LEVEL level, const char* fmt, va_list args, void*)
{
   if (level == PINMAME_LOG_LEVEL_ERROR)
   {
      char buf[1024];
      vsnprintf(buf, sizeof(buf), fmt, args);
      printf("ERROR\t%s\t%s\n", g_rom, buf);
   }
}
static void PINMAMECALLBACK OnSound(int, int, void*) {}

int main(int argc, char** argv)
{
   if (argc < 3)
      return 2;
   g_rom = argv[2];
   PinmameConfig config = { PINMAME_AUDIO_FORMAT_INT16, 44100, "", &OnState, &OnDisplayAvailable, &OnDisplayUpdated, &OnAudioAvailable,
      &OnAudioUpdated, &OnMech, &OnMech, &OnSol, &OnConsole, &IsKey, &OnLog, &OnSound };
   snprintf((char*)config.vpmPath, PINMAME_MAX_PATH, "%s", argv[1]);
   PinmameSetConfig(&config);
   PinmameSetHandleKeyboard(0);
   PinmameSetHandleMechanics(0);
   PinmameGetGame(g_rom, &OnGame, nullptr);
   const PINMAME_STATUS status = PinmameRun(g_rom);
   if (status != PINMAME_STATUS_OK)
   {
      printf("RUNFAIL\t%s\t%d\n", g_rom, (int)status);
      return 1;
   }
   std::this_thread::sleep_for(std::chrono::seconds(8));
   printf("TIMEOUT\t%s\n", g_rom);
   fflush(stdout);
   _exit(1);
}
