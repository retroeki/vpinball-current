// license:GPLv3+
//
// vbs_check: runs VBScript files through the same vendored Wine vbscript engine
// the app ships (standalone/inc/wine), on the host, and reports per file whether
// it compiles and, with --run, whether its top-level code runs.
//
// It mirrors how CodeViewer drives the engine: InitNew, SetScriptSite,
// ParseScriptText while INITIALIZED (compile only, code is queued), then a state
// change that executes the queued global code.
//
// Usage: vbs_check [--run] [--raw] [--globals=Name1,Name2] [--host=SCRIPTS_DIR [--items=DIR] [--table-dir=DIR]] file.vbs...
//   --run      also execute the top-level code (SCRIPTSTATE_STARTED)
//   --host     run a table script against a stand-in table (table_host.c): implies
//              --run, serves GetTextFile from SCRIPTS_DIR, then calls <table>_Init
//   --items    with --host: DIR/<script name>.items.tsv (extract_vpx_items.py) makes the
//              stand-in answer only the .vpx's real names, with their TypeName
//   --table-dir with --host: GetTextFile also serves the files that ship next to the
//              .vpx (DIR, then DIR/scripts), after SCRIPTS_DIR, as on device
//   --relaunch with --host: after init call <table>_Exit (where tables save high
//              scores), then start the table again on the same UserDirectory
//              (a fresh temp folder per table); failures there are relaunch-*
//   --raw      feed the bytes as-is; by default the text gets the same final pass
//              SimpleScriptPatcher::PatchScript gives every table script on device
//              (non-ASCII -> space, CR/LF -> CRLF)
//   --globals  expose a host object with SCRIPTITEM_GLOBALMEMBERS whose members
//              are these names (like ScriptGlobalTable's Version, MusicVolume, ...)
//
// One TSV line per file:
//   OK    <path>
//   FAIL  <compile|run|init|exit|relaunch-run|relaunch-init>  <line>  <char>  <code>  <path>  <description>
// <code> is the VBScript error number for 800Axxxx errors, else the hex HRESULT.
// Exit status is 1 when any file failed.

#define _GNU_SOURCE // mkdtemp, nftw
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ftw.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "oleauto.h"
#include "activscp.h"

static const GUID CLSID_VBScript_harness = { 0xb54f3741, 0x5b07, 0x11cf, { 0xa4, 0xb0, 0x00, 0xaa, 0x00, 0x4a, 0x55, 0xe8 } };
static const WCHAR HOST_GLOBAL_NAME[] = L"HarnessHostGlobal";
static const WCHAR TABLE_HOST_NAME[] = L"HarnessTable";

void table_host_init(const char* scripts_dir);
void table_host_set_table_dir(const char* dir);
void table_host_set_user_dir(const char* dir);
const WCHAR* table_host_exit_sub(void);
IDispatch* table_host_global(void);
void table_host_note_script(const WCHAR* text, size_t len);
void table_host_reset(void);
int table_host_load_items(const char* path);
const WCHAR* table_host_init_sub(void);
int table_host_item_count(void);
const WCHAR* table_host_item_name(int i);
IDispatch* table_host_item(const WCHAR* name);
static const char* g_items_dir;
static int g_table_host;
static int g_relaunch;

// --- first error seen for the file being checked --------------------------------

typedef struct {
   int count;
   const char* phase;
   ULONG line;
   LONG chr;
   SCODE scode;
   char desc[512];
} ErrorRecord;

static ErrorRecord g_err;
static const char* g_phase = "compile";

static void narrow(const WCHAR* src, char* dst, size_t size)
{
   size_t i = 0;
   if (src)
      for (; src[i] && i + 1 < size; i++)
         dst[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : ' ';
   dst[i] = 0;
}

// --- host global object (SCRIPTITEM_GLOBALMEMBERS) -----------------------------

static WCHAR** g_global_names;
static int g_global_count;

static HRESULT WINAPI Host_QueryInterface(IDispatch* iface, REFIID riid, void** ppv)
{
   if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDispatch)) {
      *ppv = iface;
      return S_OK;
   }
   *ppv = NULL;
   return E_NOINTERFACE;
}
static ULONG WINAPI Host_AddRef(IDispatch* iface) { return 2; }
static ULONG WINAPI Host_Release(IDispatch* iface) { return 1; }
static HRESULT WINAPI Host_GetTypeInfoCount(IDispatch* iface, UINT* pctinfo) { *pctinfo = 0; return S_OK; }
static HRESULT WINAPI Host_GetTypeInfo(IDispatch* iface, UINT i, LCID lcid, ITypeInfo** ti) { return E_NOTIMPL; }

static HRESULT WINAPI Host_GetIDsOfNames(IDispatch* iface, REFIID riid, LPOLESTR* names, UINT count, LCID lcid, DISPID* ids)
{
   for (int i = 0; i < g_global_count; i++)
      if (count >= 1 && !wcscasecmp(names[0], g_global_names[i])) {
         ids[0] = i + 1;
         return S_OK;
      }
   ids[0] = DISPID_UNKNOWN;
   return DISP_E_UNKNOWNNAME;
}

static HRESULT WINAPI Host_Invoke(IDispatch* iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
   DISPPARAMS* params, VARIANT* result, EXCEPINFO* ei, UINT* argerr)
{
   if (id < 1 || id > g_global_count)
      return DISP_E_MEMBERNOTFOUND;
   if (result) {
      V_VT(result) = VT_I4;
      V_I4(result) = 0;
   }
   return S_OK;
}

static IDispatchVtbl HostVtbl = {
   Host_QueryInterface, Host_AddRef, Host_Release,
   Host_GetTypeInfoCount, Host_GetTypeInfo, Host_GetIDsOfNames, Host_Invoke
};
static IDispatch g_host_global = { &HostVtbl };

// --- script site ----------------------------------------------------------------

static HRESULT WINAPI Site_QueryInterface(IActiveScriptSite* iface, REFIID riid, void** ppv)
{
   if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IActiveScriptSite)) {
      *ppv = iface;
      return S_OK;
   }
   *ppv = NULL;
   return E_NOINTERFACE;
}
static ULONG WINAPI Site_AddRef(IActiveScriptSite* iface) { return 2; }
static ULONG WINAPI Site_Release(IActiveScriptSite* iface) { return 1; }
static HRESULT WINAPI Site_GetLCID(IActiveScriptSite* iface, LCID* lcid) { return E_NOTIMPL; }

static HRESULT WINAPI Site_GetItemInfo(IActiveScriptSite* iface, LPCOLESTR name, DWORD mask, IUnknown** unk, ITypeInfo** ti)
{
   if (ti)
      *ti = NULL;
   if (unk)
      *unk = NULL;
   IDispatch* disp = NULL;
   if (!wcscmp(name, HOST_GLOBAL_NAME))
      disp = &g_host_global;
   else if (g_table_host && !wcscmp(name, TABLE_HOST_NAME))
      disp = table_host_global();
   else if (g_table_host)
      disp = table_host_item(name);
   if (!disp)
      return TYPE_E_ELEMENTNOTFOUND;
   if ((mask & SCRIPTINFO_IUNKNOWN) && unk) {
      *unk = (IUnknown*)disp;
      IUnknown_AddRef(*unk);
   }
   return S_OK;
}

static HRESULT WINAPI Site_GetDocVersionString(IActiveScriptSite* iface, BSTR* v) { return E_NOTIMPL; }
static HRESULT WINAPI Site_OnScriptTerminate(IActiveScriptSite* iface, const VARIANT* r, const EXCEPINFO* e) { return S_OK; }
static HRESULT WINAPI Site_OnStateChange(IActiveScriptSite* iface, SCRIPTSTATE s) { return S_OK; }

static HRESULT WINAPI Site_OnScriptError(IActiveScriptSite* iface, IActiveScriptError* err)
{
   DWORD ctx = 0;
   ULONG line = 0;
   LONG chr = 0;
   EXCEPINFO ei;
   memset(&ei, 0, sizeof(ei));
   IActiveScriptError_GetSourcePosition(err, &ctx, &line, &chr);
   IActiveScriptError_GetExceptionInfo(err, &ei);
   if (g_err.count++ == 0) {
      g_err.phase = g_phase;
      g_err.line = line + 1; // 0-based, CodeViewer::OnScriptError adds one too
      g_err.chr = chr;
      g_err.scode = ei.scode;
      narrow(ei.bstrDescription, g_err.desc, sizeof(g_err.desc));
   }
   SysFreeString(ei.bstrSource);
   SysFreeString(ei.bstrDescription);
   SysFreeString(ei.bstrHelpFile);
   return S_OK;
}

static HRESULT WINAPI Site_OnEnterScript(IActiveScriptSite* iface) { return S_OK; }
static HRESULT WINAPI Site_OnLeaveScript(IActiveScriptSite* iface) { return S_OK; }

static IActiveScriptSiteVtbl SiteVtbl = {
   Site_QueryInterface, Site_AddRef, Site_Release, Site_GetLCID, Site_GetItemInfo,
   Site_GetDocVersionString, Site_OnScriptTerminate, Site_OnStateChange, Site_OnScriptError,
   Site_OnEnterScript, Site_OnLeaveScript
};
static IActiveScriptSite g_site = { &SiteVtbl };

// --- input ------------------------------------------------------------------------

static WCHAR* load_script(const char* path, int raw)
{
   FILE* f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   const long size = ftell(f);
   fseek(f, 0, SEEK_SET);
   unsigned char* bytes = malloc(size > 0 ? size : 1);
   const size_t got = fread(bytes, 1, size, f);
   fclose(f);

   // worst case every byte is a lone LF that becomes CRLF
   WCHAR* text = malloc((got * 2 + 1) * sizeof(WCHAR));
   size_t n = 0;
   for (size_t i = 0; i < got; i++) {
      const unsigned char c = bytes[i];
      if (raw)
         text[n++] = c;
      else if (c > 127)
         text[n++] = ' ';
      else if (c == '\r') {
         text[n++] = '\r';
         text[n++] = '\n';
         if (i + 1 < got && bytes[i + 1] == '\n')
            i++;
      }
      else if (c == '\n') {
         text[n++] = '\r';
         text[n++] = '\n';
      }
      else
         text[n++] = c;
   }
   text[n] = 0;
   free(bytes);
   return text;
}

// --- one file ---------------------------------------------------------------------

// Calls a Sub the script defines, if it does (VPX fires <table>_Init and _Exit).
static void call_sub(IActiveScript* script, const WCHAR* name, const char* phase)
{
   IDispatch* disp = NULL;
   if (FAILED(IActiveScript_GetScriptDispatch(script, NULL, &disp)) || !disp)
      return;
   LPOLESTR sub_name = (LPOLESTR)name;
   DISPID id;
   if (SUCCEEDED(IDispatch_GetIDsOfNames(disp, &IID_NULL, &sub_name, 1, 0, &id))) {
      DISPPARAMS none = { NULL, NULL, 0, 0 };
      VARIANT r;
      VariantInit(&r);
      g_phase = phase;
      IDispatch_Invoke(disp, id, &IID_NULL, 0, DISPATCH_METHOD, &none, &r, NULL, NULL);
      VariantClear(&r);
   }
   IDispatch_Release(disp);
}

static int remove_entry(const char* p, const struct stat* sb, int flag, struct FTW* ftw) { return remove(p); }

// One start of the table. A second launch (--relaunch) reads back what the first
// one's <table>_Exit saved; its phases are reported as relaunch-*.
static int launch(const char* path, const WCHAR* text, int run, int second, int call_exit)
{
   g_phase = second ? "relaunch-compile" : "compile";
   IActiveScript* script = NULL;
   IActiveScriptParse* parse = NULL;
   HRESULT hr = CoCreateInstance(&CLSID_VBScript_harness, NULL, CLSCTX_INPROC_SERVER, &IID_IActiveScript, (void**)&script);
   if (SUCCEEDED(hr))
      hr = IActiveScript_QueryInterface(script, &IID_IActiveScriptParse, (void**)&parse);
   if (FAILED(hr)) {
      printf("FAIL\tengine\t0\t0\t%08x\t%s\tcannot create the VBScript engine\n", (unsigned)hr, path);
      return 1;
   }

   parse->lpVtbl->InitNew(parse);
   IActiveScript_SetScriptSite(script, &g_site);
   if (g_global_count > 0)
      IActiveScript_AddNamedItem(script, HOST_GLOBAL_NAME, SCRIPTITEM_ISVISIBLE | SCRIPTITEM_GLOBALMEMBERS);
   if (g_table_host) {
      table_host_reset();
      if (g_items_dir) {
         // DIR/<file name without .vbs>.items.tsv
         const char* base = strrchr(path, '/');
         base = base ? base + 1 : path;
         char items[2048];
         snprintf(items, sizeof(items), "%s/%.*s.items.tsv", g_items_dir,
            (int)(strlen(base) > 4 ? strlen(base) - 4 : strlen(base)), base);
         if (!table_host_load_items(items)) {
            printf("FAIL\tio\t0\t0\t-\t%s\tno items file %s\n", path, items);
            IActiveScript_Close(script);
            parse->lpVtbl->Release(parse);
            IActiveScript_Release(script);
            return 1;
         }
      }
      table_host_note_script(text, wcslen(text));
      IActiveScript_AddNamedItem(script, TABLE_HOST_NAME, SCRIPTITEM_ISVISIBLE | SCRIPTITEM_GLOBALMEMBERS);
      for (int i = 0; i < table_host_item_count(); i++)
         IActiveScript_AddNamedItem(script, table_host_item_name(i), SCRIPTITEM_ISSOURCE | SCRIPTITEM_ISVISIBLE);
   }

   EXCEPINFO ei;
   memset(&ei, 0, sizeof(ei));
   hr = parse->lpVtbl->ParseScriptText(parse, text, NULL, NULL, NULL, 0, 0, SCRIPTTEXT_ISVISIBLE, NULL, &ei);
   if (FAILED(hr) && g_err.count == 0) {
      g_err.count = 1;
      g_err.phase = "compile";
      g_err.scode = hr;
      snprintf(g_err.desc, sizeof(g_err.desc), "ParseScriptText failed without a site error");
   }

   if (run && g_err.count == 0) {
      g_phase = second ? "relaunch-run" : "run";
      IActiveScript_SetScriptState(script, SCRIPTSTATE_STARTED);
   }

   // VPX fires Table1_Init once the script is running; most tables do their
   // setup (vpmInit, ball stacks, timers) there rather than at top level.
   if (g_table_host && g_err.count == 0)
      call_sub(script, table_host_init_sub(), second ? "relaunch-init" : "init");
   // Quitting fires <table>_Exit, where tables save high scores and settings.
   if (call_exit && g_err.count == 0)
      call_sub(script, table_host_exit_sub(), "exit");

   IActiveScript_Close(script);
   parse->lpVtbl->Release(parse);
   IActiveScript_Release(script);
   return 0;
}

static int check_file(const char* path, int run, int raw)
{
   memset(&g_err, 0, sizeof(g_err));
   g_phase = "compile";

   WCHAR* text = load_script(path, raw);
   if (!text) {
      printf("FAIL\tio\t0\t0\t-\t%s\tcannot read file\n", path);
      return 1;
   }

   // Each table gets its own empty UserDirectory, removed afterwards.
   char user_dir[] = "/tmp/vbs_check_user_XXXXXX";
   const int own_user_dir = g_table_host && mkdtemp(user_dir) != NULL;
   if (own_user_dir)
      table_host_set_user_dir(user_dir);

   const int launches = g_table_host && g_relaunch ? 2 : 1;
   int printed = 0;
   for (int i = 0; i < launches && g_err.count == 0 && !printed; i++)
      printed = launch(path, text, run, i > 0, i + 1 < launches);

   if (own_user_dir && getenv("VBS_HARNESS_KEEP_USER_DIR"))
      fprintf(stderr, "UserDirectory kept: %s\n", user_dir);
   else if (own_user_dir)
      nftw(user_dir, remove_entry, 8, FTW_DEPTH | FTW_PHYS);
   free(text);
   if (printed)
      return 1;

   if (g_err.count == 0) {
      printf("OK\t%s\n", path);
      return 0;
   }
   char code[16];
   if (((unsigned)g_err.scode & 0xFFFF0000u) == 0x800A0000u)
      snprintf(code, sizeof(code), "%u", (unsigned)g_err.scode & 0xFFFFu);
   else
      snprintf(code, sizeof(code), "%08x", (unsigned)g_err.scode);
   printf("FAIL\t%s\t%lu\t%ld\t%s\t%s\t%s\n", g_err.phase, (unsigned long)g_err.line, (long)g_err.chr, code, path, g_err.desc);
   return 1;
}

static void parse_globals(const char* list)
{
   char* copy = strdup(list);
   for (char* tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
      const size_t len = strlen(tok);
      WCHAR* w = malloc((len + 1) * sizeof(WCHAR));
      for (size_t i = 0; i <= len; i++)
         w[i] = (unsigned char)tok[i];
      g_global_names = realloc(g_global_names, (g_global_count + 1) * sizeof(WCHAR*));
      g_global_names[g_global_count++] = w;
   }
   free(copy);
}

int main(int argc, char** argv)
{
   int run = 0, raw = 0, failed = 0, files = 0;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--run"))
         run = 1;
      else if (!strcmp(argv[i], "--raw"))
         raw = 1;
      else if (!strncmp(argv[i], "--globals=", 10))
         parse_globals(argv[i] + 10);
      else if (!strncmp(argv[i], "--items=", 8))
         g_items_dir = argv[i] + 8;
      else if (!strcmp(argv[i], "--relaunch"))
         g_relaunch = 1;
      else if (!strncmp(argv[i], "--table-dir=", 12))
         table_host_set_table_dir(argv[i] + 12);
      else if (!strncmp(argv[i], "--host=", 7)) {
         table_host_init(argv[i] + 7);
         g_table_host = 1;
         run = 1;
      }
      else {
         failed |= check_file(argv[i], run, raw);
         files++;
         fflush(stdout);
      }
   }
   if (files == 0) {
      fprintf(stderr, "usage: vbs_check [--run] [--raw] [--globals=A,B] file.vbs...\n");
      return 2;
   }
   return failed;
}
