// license:GPLv3+
//
// Host-side stand-ins for the external_* hooks that the vendored Wine sources
// call back into (the engine implements them in src/core/def.cpp). Only what the
// script engine needs to parse, compile and run plain VBScript: no table, no
// plugins, no COM object overrides.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"

static int verbose(void)
{
   static int cached = -1;
   if (cached < 0) {
      const char* env = getenv("VBS_HARNESS_VERBOSE");
      cached = (env && *env && *env != '0') ? 1 : 0;
   }
   return cached;
}

static void vlog(const char* level, const char* format, va_list args)
{
   if (!verbose())
      return;
   fprintf(stderr, "[%s] ", level);
   vfprintf(stderr, format, args);
   fputc('\n', stderr);
}

void external_log_info(const char* format, ...)
{
   va_list args;
   va_start(args, format);
   vlog("info", format, args);
   va_end(args);
}

void external_log_debug(const char* format, ...)
{
   va_list args;
   va_start(args, format);
   vlog("debug", format, args);
   va_end(args);
}

void external_log_error(const char* format, ...)
{
   va_list args;
   va_start(args, format);
   vlog("error", format, args);
   va_end(args);
}

int external_diag_enabled(void)
{
   static int cached = -1;
   if (cached < 0) {
      const char* env = getenv("VPX_SCRIPT_DIAG");
      cached = (env && *env && *env != '0') ? 1 : 0;
   }
   return cached;
}

IUnknown* table_host_create_object(const WCHAR* progid);

// The engine creates WScript.Shell / ArrayList / Shell.Application here. Without
// --host the harness has none of them, so CreateObject fails like any unknown
// ProgID (VBSE_CANT_CREATE_OBJECT); with --host every ProgID gets the stand-in
// table's catch-all object. The third parameter is really an IUnknown** (see
// def.cpp).
HRESULT external_create_object(const WCHAR* progid, IClassFactory* cf, IUnknown* obj)
{
   IUnknown** ppObj = (IUnknown**)obj;
   (void)cf;
   if (ppObj)
      *ppObj = table_host_create_object(progid);
   return (ppObj && *ppObj) ? S_OK : CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT external_open_storage(const OLECHAR* pwcsName, IStorage* pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage** ppstgOpen)
{
   (void)pwcsName;
   (void)pstgPriority;
   (void)grfMode;
   (void)snbExclude;
   (void)reserved;
   if (ppstgOpen)
      *ppstgOpen = NULL;
   return E_NOTIMPL;
}
