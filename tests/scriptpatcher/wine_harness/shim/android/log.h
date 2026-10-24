// license:GPLv3+
// Host stand-in for the NDK's <android/log.h>: interp.c includes it under
// __STANDALONE__ (the fork only builds standalone for Android). Messages go to
// stderr only when VBS_HARNESS_VERBOSE is set.
#pragma once
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6 };

static inline int __android_log_print(int prio, const char* tag, const char* fmt, ...)
{
   const char* env = getenv("VBS_HARNESS_VERBOSE");
   if (!env || !*env || *env == '0')
      return 0;
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "%s: ", tag);
   vfprintf(stderr, fmt, args);
   fputc('\n', stderr);
   va_end(args);
   return 0;
}
