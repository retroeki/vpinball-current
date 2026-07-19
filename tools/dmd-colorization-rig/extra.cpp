#include <cstdarg>
#include "serum-decode.cpp"
int main(int argc, char** argv) {
  Serum_Frame_Struc* s = Serum_Load("altcolor", argv[1], FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES);
  if (!s) return 1;
  int extra = 0;
  for (uint32_t i = 0; i < g_serumData.nframes; i++)
    if (g_serumData.isextraframe[i][0]) extra++;
  printf("nframes=%u extra=%d fw=%u fh=%u fwx=%u fhx=%u nocolors=%u\n",
    g_serumData.nframes, extra, g_serumData.fwidth, g_serumData.fheight,
    g_serumData.fwidth_extra, g_serumData.fheight_extra, g_serumData.nocolors);
  if (argc > 2) {
    for (int k = 2; k < argc; k++) {
      uint32_t i = (uint32_t)atoi(argv[k]);
      printf("frame %u: isextra=%u trig=%08x mask=%u\n", i,
        (unsigned)g_serumData.isextraframe[i][0], g_serumData.triggerIDs[i][0],
        (unsigned)g_serumData.compmaskID[i][0]);
    }
  }
  return 0;
}
