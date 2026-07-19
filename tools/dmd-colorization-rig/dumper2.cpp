// v7-capable hashcode dumper built against current PPUC/libserum_concentrate.
// Prints: frameIdx hash shapecompmode compmaskID for every frame.
#include <cstdarg>
#include "serum-decode.cpp"

static void DumpLog(const char* format, va_list args, const void* userData)
{
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: dumper2 <altcolorpath> <romname>\n"); return 2; }
  Serum_SetLogCallback(DumpLog, nullptr);
  Serum_Frame_Struc* s = Serum_Load(argv[1], argv[2], FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES);
  if (!s) { fprintf(stderr, "RESULT: load failed\n"); return 1; }
  fprintf(stderr, "RESULT: SerumVersion=%u nframes=%u ncompmasks=%u w=%u h=%u wx=%u hx=%u\n",
          (unsigned)s->SerumVersion, g_serumData.nframes, g_serumData.ncompmasks,
          g_serumData.fwidth, g_serumData.fheight, g_serumData.fwidth_extra, g_serumData.fheight_extra);
  for (uint32_t i = 0; i < g_serumData.nframes; i++)
    printf("%u %08x shape=%u mask=%u\n", i,
           g_serumData.hashcodes[i][0],
           (unsigned)g_serumData.shapecompmode[i][0],
           (unsigned)g_serumData.compmaskID[i][0]);
  return 0;
}
