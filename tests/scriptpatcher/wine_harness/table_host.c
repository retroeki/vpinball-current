// license:GPLv3+
//
// A stand-in table for vbs_check --host: enough host for a table script's
// top-level code and <table>_Init to run under the shipped engine.
//
// A global object registered with SCRIPTITEM_GLOBALMEMBERS plays the table: Wine
// consults it last (after script variables, functions and its own built-ins).
//
// With --items (from tests/scriptpatcher/extract_vpx_items.py) it answers the
// names the device would: ScriptGlobalTable's members, the table, every game
// item and every collection. Elements report their real TypeName (core.vbs
// checks TypeName(aKicker) = "Kicker"), collections enumerate their members,
// and a name the .vpx does not have stays undefined, as on device.
//
// Without --items every name resolves (a catch-all), except names the scripts
// declare with Const or ReDim, which Wine must be able to create itself.
//
// Either way, members of these objects are catch-alls: assigned values are
// remembered, an unset member reads as another catch-all (so
// Controller.Games(cGameName).Settings.Value("sound") = 0 works) whose value is
// 1, ShowDT/ShowFSS and Is* probes read False (the app forces cabinet mode;
// IsRendering loops must end), and .Name of an
// element is its name. GetTextFile serves the bundled VPM scripts, so LoadVPM
// really runs controller.vbs / <system>.vbs / core.vbs. CreateObject gives a
// catch-all for the ProgIDs the device provides and fails for the rest.
//
// What it cannot tell: anything a real object would compute or return.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "oleauto.h"

typedef struct {
   WCHAR* name;
   VARIANT value;
   int has_value;
} Slot;

typedef struct AnyObject {
   IDispatch IDispatch_iface;
   LONG ref;
   int is_global;
   WCHAR* label;     // the name this object stands for: its .Name
   WCHAR* type_name; // TypeName(); NULL reports "Object"
   struct AnyObject** members; // a collection's items
   int member_count;
   Slot* slots;
   int count, cap;
   int is_pup_player; // CreateObject("PinUpPlayer.PinDisplay")
} AnyObject;

static char g_scripts_dir[1024];
static char g_table_dir[1024]; // --table-dir: files shipped next to the .vpx
static AnyObject* g_global;

static AnyObject* any_new(int is_global);

// --- names from the .vpx (--items) -------------------------------------------------

typedef struct {
   WCHAR* name;
   AnyObject* obj;
} Item;

static Item* g_items;
static int g_item_count;
static int g_faithful;
static WCHAR g_init_sub[300] = L"Table1_Init";
static WCHAR g_exit_sub[300] = L"Table1_Exit";
static WCHAR g_user_dir[1024]; // UserDirectory, with its trailing separator

static Item* find_item(const WCHAR* name)
{
   for (int i = 0; i < g_item_count; i++)
      if (!wcscasecmp(g_items[i].name, name))
         return &g_items[i];
   return NULL;
}

static WCHAR* widen_utf8(const char* s, size_t n)
{
   WCHAR* w = malloc((n + 1) * sizeof(WCHAR));
   size_t k = 0;
   for (size_t i = 0; i < n;) {
      const unsigned char c = (unsigned char)s[i];
      unsigned cp;
      int extra;
      if (c < 0x80) { cp = c; extra = 0; }
      else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
      else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
      else { cp = c & 0x07; extra = 3; }
      i++;
      while (extra-- > 0 && i < n)
         cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
      w[k++] = (WCHAR)cp;
   }
   w[k] = 0;
   return w;
}

static void add_item(WCHAR* name, const WCHAR* type)
{
   g_items = realloc(g_items, (g_item_count + 1) * sizeof(Item));
   AnyObject* o = any_new(0);
   o->label = wcsdup(name);
   o->type_name = wcsdup(type);
   g_items[g_item_count].name = name;
   g_items[g_item_count].obj = o;
   g_item_count++;
}

static int new_slot(AnyObject* This, const WCHAR* name);

// "TimerInterval=12;Material=Plastic": preset members, numbers as Long.
static void set_props(AnyObject* o, char* props)
{
   for (char* kv = strtok(props, ";"); kv; kv = strtok(NULL, ";")) {
      char* eq = strchr(kv, '=');
      if (!eq)
         continue;
      *eq = 0;
      WCHAR* key = widen_utf8(kv, strlen(kv));
      const int id = new_slot(o, key); // may move o->slots: index after the call
      Slot* s = &o->slots[id - 1];
      free(key);
      char* end;
      const long n = strtol(eq + 1, &end, 10);
      if (eq[1] && end != eq + 1 && *end == 0) {
         V_VT(&s->value) = VT_I4;
         V_I4(&s->value) = (LONG)n;
      }
      else {
         WCHAR* w = widen_utf8(eq + 1, strlen(eq + 1));
         V_VT(&s->value) = VT_BSTR;
         V_BSTR(&s->value) = SysAllocString(w);
         free(w);
      }
      s->has_value = 1;
   }
}

int table_host_load_items(const char* path)
{
   FILE* f = fopen(path, "rb");
   if (!f)
      return 0;
   char line[65536];
   // collection members are resolved after every item is known
   char** pending = NULL;
   int pending_count = 0;
   while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      char* tab1 = strchr(line, '\t');
      if (!tab1)
         continue;
      *tab1 = 0;
      char* type = tab1 + 1;
      char* tab2 = strchr(type, '\t');
      if (tab2)
         *tab2 = 0;
      WCHAR* name = widen_utf8(line, strlen(line));
      WCHAR* wtype = widen_utf8(type, strlen(type));
      if (find_item(name)) {
         free(name);
         free(wtype);
         continue;
      }
      add_item(name, wtype);
      if (strcmp(type, "Collection") && tab2 && tab2[1])
         set_props(g_items[g_item_count - 1].obj, tab2 + 1);
      if (!strcmp(type, "Table")) {
         swprintf(g_init_sub, sizeof(g_init_sub) / sizeof(WCHAR), L"%ls_Init", name);
         swprintf(g_exit_sub, sizeof(g_exit_sub) / sizeof(WCHAR), L"%ls_Exit", name);
      }
      if (!strcmp(type, "Collection") && tab2) {
         pending = realloc(pending, (pending_count + 1) * sizeof(char*));
         size_t n = strlen(line) + 1 + strlen(tab2 + 1) + 1;
         pending[pending_count] = malloc(n);
         snprintf(pending[pending_count], n, "%s\t%s", line, tab2 + 1);
         pending_count++;
      }
      free(wtype);
   }
   fclose(f);
   // CodeViewer also registers the debugger module as a named item: debug.print works.
   if (!find_item(L"Debug"))
      add_item(wcsdup(L"Debug"), L"VPDebug");
   for (int i = 0; i < pending_count; i++) {
      char* tab = strchr(pending[i], '\t');
      *tab = 0;
      WCHAR* cname = widen_utf8(pending[i], strlen(pending[i]));
      AnyObject* col = find_item(cname)->obj;
      free(cname);
      for (char* m = strtok(tab + 1, ","); m; m = strtok(NULL, ",")) {
         WCHAR* mname = widen_utf8(m, strlen(m));
         Item* it = find_item(mname);
         free(mname);
         if (!it)
            continue;
         col->members = realloc(col->members, (col->member_count + 1) * sizeof(AnyObject*));
         col->members[col->member_count++] = it->obj;
      }
      free(pending[i]);
   }
   free(pending);
   g_faithful = 1;
   return 1;
}

const WCHAR* table_host_init_sub(void) { return g_init_sub; }
const WCHAR* table_host_exit_sub(void) { return g_exit_sub; }

// With --items each element is its own named item (SCRIPTITEM_ISSOURCE |
// SCRIPTITEM_ISVISIBLE), as CodeViewer registers them; only ScriptGlobalTable's
// members come through the global-members object.
int table_host_item_count(void) { return g_item_count; }
const WCHAR* table_host_item_name(int i) { return g_items[i].name; }

IDispatch* table_host_item(const WCHAR* name)
{
   Item* it = find_item(name);
   return it ? &it->obj->IDispatch_iface : NULL;
}

// --- names the catch-all must not claim (without --items) ----------------------------
//
// Wine creates a global Const only when its statement runs, and interp_const
// refuses the name if anything already answers it. The device's global object
// answers a fixed list (ScriptGlobalTable); the catch-all answers everything.
// So every name the scripts declare as Const is left alone, unless the device
// would answer it too. Same for ReDim: at top level it declares the variable, but
// only if nothing answers the name first.

#include "sgt_names.inc"

static WCHAR** g_const_names;
static int g_const_count;

static int is_sgt_name(const WCHAR* name)
{
   for (size_t i = 0; i < sizeof(kScriptGlobalTableNames) / sizeof(kScriptGlobalTableNames[0]); i++)
      if (!wcscasecmp(kScriptGlobalTableNames[i], name))
         return 1;
   return 0;
}

static int is_const_name(const WCHAR* name)
{
   for (int i = 0; i < g_const_count; i++)
      if (!wcscasecmp(g_const_names[i], name))
         return 1;
   return 0;
}

static int is_ident(WCHAR c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static int starts_word(const WCHAR* p, const WCHAR* end, const char* word)
{
   size_t n = strlen(word);
   if ((size_t)(end - p) < n)
      return 0;
   for (size_t i = 0; i < n; i++) {
      WCHAR c = p[i];
      if (c >= 'A' && c <= 'Z')
         c += 'a' - 'A';
      if (c != (WCHAR)word[i])
         return 0;
   }
   return (size_t)(end - p) == n || !is_ident(p[n]);
}

static void add_const_name(const WCHAR* p, size_t len)
{
   WCHAR* name = malloc((len + 1) * sizeof(WCHAR));
   memcpy(name, p, len * sizeof(WCHAR));
   name[len] = 0;
   if (is_const_name(name) || is_sgt_name(name)) {
      free(name);
      return;
   }
   g_const_names = realloc(g_const_names, (g_const_count + 1) * sizeof(WCHAR*));
   g_const_names[g_const_count++] = name;
}

// Names after a declaration keyword: `a = 1, b = "x"` or `a(n), b(m)`. Commas
// inside parentheses or strings do not start a new name.
static void scan_names(const WCHAR* p, const WCHAR* end)
{
   int expect_name = 1, depth = 0, in_str = 0;
   while (p < end) {
      if (expect_name) {
         while (p < end && (*p == ' ' || *p == '\t'))
            p++;
         const WCHAR* s = p;
         while (p < end && is_ident(*p))
            p++;
         if (p > s)
            add_const_name(s, p - s);
         expect_name = 0;
         continue;
      }
      const WCHAR c = *p++;
      if (c == '"')
         in_str = !in_str;
      else if (!in_str && c == '(')
         depth++;
      else if (!in_str && c == ')')
         depth--;
      else if (!in_str && depth == 0 && c == ',')
         expect_name = 1;
   }
}

// One statement (no comment, no ':' outside strings):
// `[Public|Private] Const a = 1, b = "x"` or `ReDim [Preserve] a(n), b(m)`.
static void scan_statement(const WCHAR* p, const WCHAR* end)
{
   while (p < end && (*p == ' ' || *p == '\t'))
      p++;
   if (starts_word(p, end, "redim")) {
      p += 5;
      while (p < end && (*p == ' ' || *p == '\t'))
         p++;
      if (starts_word(p, end, "preserve"))
         p += 8;
      scan_names(p, end);
      return;
   }
   if (starts_word(p, end, "public") || starts_word(p, end, "private")) {
      while (p < end && is_ident(*p))
         p++;
      while (p < end && (*p == ' ' || *p == '\t'))
         p++;
   }
   if (!starts_word(p, end, "const"))
      return;
   scan_names(p + 5, end);
}

void table_host_note_script(const WCHAR* text, size_t len)
{
   const WCHAR* end = text + len;
   const WCHAR* stmt = text;
   int in_str = 0, in_comment = 0;
   for (const WCHAR* p = text; p <= end; p++) {
      const WCHAR c = p < end ? *p : '\n';
      if (c == '\r' || c == '\n') {
         if (!in_comment)
            scan_statement(stmt, p);
         stmt = p + 1;
         in_str = in_comment = 0;
      }
      else if (in_comment)
         continue;
      else if (c == '"')
         in_str = !in_str;
      else if (!in_str && c == '\'') {
         scan_statement(stmt, p);
         in_comment = 1;
      }
      else if (!in_str && c == ':') {
         scan_statement(stmt, p);
         stmt = p + 1;
      }
   }
}

// --- enumerator over a collection's members (For Each) -----------------------------

typedef struct {
   IEnumVARIANT IEnumVARIANT_iface;
   LONG ref;
   AnyObject* col;
   int pos;
} Enum;

static Enum* enum_impl(IEnumVARIANT* iface) { return (Enum*)iface; }

static HRESULT WINAPI Enum_QueryInterface(IEnumVARIANT* iface, REFIID riid, void** ppv)
{
   if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IEnumVARIANT)) {
      *ppv = iface;
      IEnumVARIANT_AddRef(iface);
      return S_OK;
   }
   *ppv = NULL;
   return E_NOINTERFACE;
}
static ULONG WINAPI Enum_AddRef(IEnumVARIANT* iface) { return InterlockedIncrement(&enum_impl(iface)->ref); }
static ULONG WINAPI Enum_Release(IEnumVARIANT* iface) { return InterlockedDecrement(&enum_impl(iface)->ref); }

static HRESULT WINAPI Enum_Next(IEnumVARIANT* iface, ULONG celt, VARIANT* v, ULONG* fetched)
{
   Enum* This = enum_impl(iface);
   const int count = This->col ? This->col->member_count : 0;
   ULONG n = 0;
   while (n < celt && This->pos < count) {
      V_VT(&v[n]) = VT_DISPATCH;
      V_DISPATCH(&v[n]) = &This->col->members[This->pos++]->IDispatch_iface;
      IDispatch_AddRef(V_DISPATCH(&v[n]));
      n++;
   }
   if (fetched)
      *fetched = n;
   return n == celt ? S_OK : S_FALSE;
}
static HRESULT WINAPI Enum_Skip(IEnumVARIANT* iface, ULONG celt) { enum_impl(iface)->pos += celt; return S_OK; }
static HRESULT WINAPI Enum_Reset(IEnumVARIANT* iface) { enum_impl(iface)->pos = 0; return S_OK; }
static HRESULT WINAPI Enum_Clone(IEnumVARIANT* iface, IEnumVARIANT** out) { return E_NOTIMPL; }

static IEnumVARIANTVtbl EnumVtbl = { Enum_QueryInterface, Enum_AddRef, Enum_Release, Enum_Next, Enum_Skip, Enum_Reset, Enum_Clone };

static IUnknown* enum_new(AnyObject* col)
{
   Enum* e = calloc(1, sizeof(Enum));
   e->IEnumVARIANT_iface.lpVtbl = &EnumVtbl;
   e->ref = 1;
   e->col = col;
   return (IUnknown*)&e->IEnumVARIANT_iface;
}

// --- TypeName (Wine asks GetTypeInfo + GetDocumentation) ----------------------------

typedef struct {
   ITypeInfo ITypeInfo_iface;
   const WCHAR* name;
} TypeInfoStub;

static HRESULT WINAPI TI_QueryInterface(ITypeInfo* iface, REFIID riid, void** ppv) { *ppv = iface; return S_OK; }
static ULONG WINAPI TI_AddRef(ITypeInfo* iface) { return 2; }
static ULONG WINAPI TI_Release(ITypeInfo* iface) { return 1; }
static HRESULT WINAPI TI_GetDocumentation(ITypeInfo* iface, MEMBERID id, BSTR* name, BSTR* doc, DWORD* ctx, BSTR* file)
{
   if (name)
      *name = SysAllocString(((TypeInfoStub*)iface)->name);
   if (doc)
      *doc = NULL;
   if (file)
      *file = NULL;
   return S_OK;
}
static HRESULT WINAPI TI_NotImpl(void) { return E_NOTIMPL; }

// Wine's TypeName only calls GetDocumentation and Release; the rest refuse.
static ITypeInfoVtbl TypeInfoVtbl = {
   .QueryInterface = TI_QueryInterface, .AddRef = TI_AddRef, .Release = TI_Release,
   .GetTypeAttr = (void*)TI_NotImpl, .GetTypeComp = (void*)TI_NotImpl, .GetFuncDesc = (void*)TI_NotImpl,
   .GetVarDesc = (void*)TI_NotImpl, .GetNames = (void*)TI_NotImpl, .GetRefTypeOfImplType = (void*)TI_NotImpl,
   .GetImplTypeFlags = (void*)TI_NotImpl, .GetIDsOfNames = (void*)TI_NotImpl, .Invoke = (void*)TI_NotImpl,
   .GetDocumentation = TI_GetDocumentation, .GetDllEntry = (void*)TI_NotImpl, .GetRefTypeInfo = (void*)TI_NotImpl,
   .AddressOfMember = (void*)TI_NotImpl, .CreateInstance = (void*)TI_NotImpl, .GetMops = (void*)TI_NotImpl,
   .GetContainingTypeLib = (void*)TI_NotImpl, .ReleaseTypeAttr = (void*)TI_NotImpl,
   .ReleaseFuncDesc = (void*)TI_NotImpl, .ReleaseVarDesc = (void*)TI_NotImpl,
};

// --- GetTextFile -----------------------------------------------------------------

static HRESULT get_text_file(const VARIANT* arg, VARIANT* result)
{
   // Called from a Sub the name arrives by reference (VT_BYREF|VT_VARIANT).
   VARIANT str;
   VariantInit(&str);
   if (!arg || FAILED(VariantCopyInd(&str, arg)) || FAILED(VariantChangeType(&str, &str, 0, VT_BSTR)))
      return E_INVALIDARG;
   char name[512];
   size_t i = 0;
   for (const WCHAR* p = V_BSTR(&str); *p && i + 1 < sizeof(name); p++)
      name[i++] = (char)*p;
   name[i] = 0;
   VariantClear(&str);

   // ScriptGlobalTable::GetTextFile order: the app's scripts, then the table's
   // folder, then its scripts subfolder.
   char path[1600];
   snprintf(path, sizeof(path), "%s/%s", g_scripts_dir, name);
   FILE* f = fopen(path, "rb");
   if (!f && g_table_dir[0]) {
      snprintf(path, sizeof(path), "%s/%s", g_table_dir, name);
      f = fopen(path, "rb");
   }
   if (!f && g_table_dir[0]) {
      snprintf(path, sizeof(path), "%s/scripts/%s", g_table_dir, name);
      f = fopen(path, "rb");
   }
   if (!f)
      return E_FAIL; // ScriptGlobalTable::GetTextFile: "Unable to load file"
   fseek(f, 0, SEEK_END);
   const long size = ftell(f);
   fseek(f, 0, SEEK_SET);
   unsigned char* bytes = malloc(size > 0 ? size : 1);
   const size_t got = fread(bytes, 1, size, f);
   fclose(f);
   BSTR text = SysAllocStringLen(NULL, (UINT)got);
   for (size_t k = 0; k < got; k++)
      text[k] = bytes[k];
   free(bytes);
   table_host_note_script(text, got);
   if (result) {
      V_VT(result) = VT_BSTR;
      V_BSTR(result) = text;
   }
   else
      SysFreeString(text);
   return S_OK;
}

// --- the objects ---------------------------------------------------------------------

static AnyObject* impl(IDispatch* iface) { return (AnyObject*)iface; }

static HRESULT WINAPI Any_QueryInterface(IDispatch* iface, REFIID riid, void** ppv)
{
   if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDispatch)) {
      *ppv = iface;
      IDispatch_AddRef(iface);
      return S_OK;
   }
   *ppv = NULL;
   return E_NOINTERFACE;
}

static ULONG WINAPI Any_AddRef(IDispatch* iface) { return InterlockedIncrement(&impl(iface)->ref); }
static ULONG WINAPI Any_Release(IDispatch* iface)
{
   // Objects are never freed: one process per table, and a script can keep
   // references in places the harness cannot see.
   return InterlockedDecrement(&impl(iface)->ref);
}

static HRESULT WINAPI Any_GetTypeInfoCount(IDispatch* iface, UINT* n) { *n = impl(iface)->type_name ? 1 : 0; return S_OK; }

static HRESULT WINAPI Any_GetTypeInfo(IDispatch* iface, UINT i, LCID lcid, ITypeInfo** ti)
{
   AnyObject* This = impl(iface);
   if (!This->type_name)
      return E_NOTIMPL;
   TypeInfoStub* t = calloc(1, sizeof(TypeInfoStub));
   t->ITypeInfo_iface.lpVtbl = &TypeInfoVtbl;
   t->name = This->type_name;
   *ti = &t->ITypeInfo_iface;
   return S_OK;
}

static int new_slot(AnyObject* This, const WCHAR* name)
{
   if (This->count == This->cap) {
      This->cap = This->cap ? This->cap * 2 : 16;
      This->slots = realloc(This->slots, This->cap * sizeof(Slot));
   }
   Slot* s = &This->slots[This->count];
   s->name = wcsdup(name);
   VariantInit(&s->value);
   s->has_value = 0;
   return ++This->count;
}

static HRESULT WINAPI Any_GetIDsOfNames(IDispatch* iface, REFIID riid, LPOLESTR* names, UINT count, LCID lcid, DISPID* ids)
{
   AnyObject* This = impl(iface);
   for (int i = 0; i < This->count; i++)
      if (!wcscasecmp(This->slots[i].name, names[0])) {
         ids[0] = i + 1;
         return S_OK;
      }
   if (This->is_global && (g_faithful ? !is_sgt_name(names[0]) : is_const_name(names[0]))) {
      ids[0] = DISPID_UNKNOWN;
      return DISP_E_UNKNOWNNAME;
   }
   ids[0] = new_slot(This, names[0]);
   return S_OK;
}

static HRESULT member_at(AnyObject* col, const VARIANT* index, VARIANT* result)
{
   VARIANT i;
   VariantInit(&i);
   if (FAILED(VariantCopyInd(&i, index)) || FAILED(VariantChangeType(&i, &i, 0, VT_I4)))
      return DISP_E_TYPEMISMATCH;
   if (V_I4(&i) < 0 || V_I4(&i) >= col->member_count)
      return DISP_E_BADINDEX;
   if (result) {
      V_VT(result) = VT_DISPATCH;
      V_DISPATCH(result) = &col->members[V_I4(&i)]->IDispatch_iface;
      IDispatch_AddRef(V_DISPATCH(result));
   }
   return S_OK;
}

static HRESULT WINAPI Any_Invoke(IDispatch* iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
   DISPPARAMS* dp, VARIANT* result, EXCEPINFO* ei, UINT* argerr)
{
   AnyObject* This = impl(iface);
   const UINT args = dp ? dp->cArgs : 0;
   const int is_collection = This->type_name && !wcscmp(This->type_name, L"Collection");

   if (id == DISPID_NEWENUM) {
      if (result) {
         V_VT(result) = VT_UNKNOWN;
         V_UNKNOWN(result) = enum_new(This);
      }
      return S_OK;
   }
   if (id == DISPID_VALUE) {
      if (is_collection && args == 1)
         return member_at(This, &dp->rgvarg[0], result);
      if (result) {
         V_VT(result) = VT_I4;
         V_I4(result) = 1;
      }
      return S_OK;
   }
   if (id < 1 || id > This->count)
      return DISP_E_MEMBERNOTFOUND;
   Slot* s = &This->slots[id - 1];

   if (This->is_global && !wcscasecmp(s->name, L"GetTextFile") && args >= 1)
      return get_text_file(&dp->rgvarg[args - 1], result);
   // No balls in play while the script initializes: an empty array, as on device.
   // ScriptGlobalTable::get_UserDirectory: a real folder, so high score and
   // settings files go through the device's FSO code (scrrun + wine.c file I/O).
   if (This->is_global && g_user_dir[0] && !wcscasecmp(s->name, L"UserDirectory") && args == 0 && result) {
      V_VT(result) = VT_BSTR;
      V_BSTR(result) = SysAllocString(g_user_dir);
      return S_OK;
   }
   // PUPPinDisplay::GetGetRoot (plugins/pup): the pupvideos folder next to the table,
   // with its trailing separator, or "" when there is none.
   if (This->is_pup_player && !wcscasecmp(s->name, L"GetRoot") && args == 0 && result) {
      char root[1100] = "";
      struct stat st;
      if (g_table_dir[0]) {
         snprintf(root, sizeof(root), "%s/pupvideos", g_table_dir);
         if (stat(root, &st) || !S_ISDIR(st.st_mode))
            root[0] = 0;
         else
            strcat(root, "/");
      }
      V_VT(result) = VT_BSTR;
      V_BSTR(result) = SysAllocStringLen(NULL, (UINT)strlen(root));
      for (size_t k = 0; root[k]; k++)
         V_BSTR(result)[k] = (WCHAR)(unsigned char)root[k];
      return S_OK;
   }
   if (This->is_global && !wcscasecmp(s->name, L"GetBalls") && result) {
      V_VT(result) = VT_ARRAY | VT_VARIANT;
      V_ARRAY(result) = SafeArrayCreateVector(VT_VARIANT, 0, 0);
      return S_OK;
   }

   if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
      // rgvarg[0] is the value; with nothing else (no index arguments) it is a
      // plain assignment and is remembered.
      if (args == 1) {
         // The engine passes a script variable by reference (VT_BYREF into its
         // frame, freed when the Sub returns): keep the value, as VPX objects do.
         VariantClear(&s->value);
         VariantCopyInd(&s->value, &dp->rgvarg[0]);
         s->has_value = 1;
      }
      return S_OK;
   }

   if (!result)
      return S_OK;
   if (s->has_value) {
      // An element called with arguments is a call on its default member, e.g.
      // a collection indexed by position.
      if (args > 0 && V_VT(&s->value) == VT_DISPATCH && V_DISPATCH(&s->value))
         return IDispatch_Invoke(V_DISPATCH(&s->value), DISPID_VALUE, riid, lcid, flags, dp, result, ei, argerr);
      if (args == 0)
         return VariantCopy(result, &s->value);
   }
   if (is_collection && !wcscasecmp(s->name, L"Count") && args == 0) {
      V_VT(result) = VT_I4;
      V_I4(result) = This->member_count;
      return S_OK;
   }
   if (is_collection && !wcscasecmp(s->name, L"Item") && args == 1)
      return member_at(This, &dp->rgvarg[0], result);
   // ShowDT/ShowFSS: PinTable::get_ShowDT on Android. Is*: state probes such as
   // `Do While UltraDMD.IsRendering : Loop` must end.
   if (args == 0 && (!wcscasecmp(s->name, L"ShowDT") || !wcscasecmp(s->name, L"ShowFSS") || !wcsncasecmp(s->name, L"Is", 2))) {
      V_VT(result) = VT_BOOL;
      V_BOOL(result) = VARIANT_FALSE;
      return S_OK;
   }
   // Scripts build handler names from element names, e.g. "Sub " & aTrigger.name & "_Hit()".
   if (This->label && args == 0 && !wcscasecmp(s->name, L"Name")) {
      V_VT(result) = VT_BSTR;
      V_BSTR(result) = SysAllocString(This->label);
      return S_OK;
   }
   AnyObject* child = any_new(0);
   if (This->is_global && !g_faithful)
      child->label = wcsdup(s->name);
   V_VT(result) = VT_DISPATCH;
   V_DISPATCH(result) = &child->IDispatch_iface;
   return S_OK;
}

static IDispatchVtbl AnyVtbl = {
   Any_QueryInterface, Any_AddRef, Any_Release, Any_GetTypeInfoCount, Any_GetTypeInfo, Any_GetIDsOfNames, Any_Invoke
};

static AnyObject* any_new(int is_global)
{
   AnyObject* o = calloc(1, sizeof(AnyObject));
   o->IDispatch_iface.lpVtbl = &AnyVtbl;
   o->ref = 1;
   o->is_global = is_global;
   return o;
}

// --- host stubs with a fixed member list (def.cpp external_create_object) ---------------

typedef enum { R_EMPTY, R_ZERO, R_EMPTY_STRING, R_FALSE, R_ARG_AS_STRING } StubResult;

typedef struct {
   const WCHAR* name;
   StubResult result;
} StubMember;

// src/core/ScriptWScriptShell.cpp
static const StubMember kWshMembers[] = {
   { L"RegRead", R_EMPTY }, { L"RegWrite", R_EMPTY }, { L"RegDelete", R_EMPTY }, { L"Run", R_ZERO },
   { L"Exec", R_EMPTY }, { L"Sleep", R_EMPTY }, { L"Wait", R_EMPTY },
   { L"ExpandEnvironmentStrings", R_ARG_AS_STRING }, { L"Environment", R_EMPTY },
   { L"SpecialFolders", R_EMPTY_STRING }, { L"CurrentDirectory", R_EMPTY_STRING }, { L"AppActivate", R_FALSE },
   { L"Popup", R_ZERO }, { L"SendKeys", R_EMPTY },
};

// src/core/ScriptShellApplication.cpp: every member returns Empty.
static const StubMember kShellAppMembers[] = {
   { L"NameSpace", R_EMPTY }, { L"Open", R_EMPTY }, { L"Explore", R_EMPTY }, { L"ShellExecute", R_EMPTY },
   { L"Windows", R_EMPTY }, { L"BrowseForFolder", R_EMPTY }, { L"MinimizeAll", R_EMPTY },
   { L"UndoMinimizeAll", R_EMPTY }, { L"TileVertically", R_EMPTY }, { L"TileHorizontally", R_EMPTY },
   { L"CascadeWindows", R_EMPTY }, { L"ControlPanelItem", R_EMPTY }, { L"TrayProperties", R_EMPTY },
   { L"Help", R_EMPTY }, { L"FindFiles", R_EMPTY }, { L"Suspend", R_EMPTY }, { L"EjectPC", R_EMPTY },
   { L"SetTime", R_EMPTY }, { L"Application", R_EMPTY }, { L"Parent", R_EMPTY },
};

// src/core/ScriptPupDmdControl.cpp
static const StubMember kPupDmdMembers[] = {
   { L"DMDOpen", R_EMPTY }, { L"DMDClose", R_EMPTY }, { L"DMDPuPMirror", R_EMPTY }, { L"DMDPuPTextMirror", R_EMPTY },
};

typedef struct {
   IDispatch IDispatch_iface;
   LONG ref;
   const StubMember* members;
   int count;
} StubObject;

static StubObject* stub_impl(IDispatch* iface) { return (StubObject*)iface; }

static ULONG WINAPI Stub_AddRef(IDispatch* iface) { return InterlockedIncrement(&stub_impl(iface)->ref); }
static ULONG WINAPI Stub_Release(IDispatch* iface) { return InterlockedDecrement(&stub_impl(iface)->ref); }
static HRESULT WINAPI Stub_GetTypeInfoCount(IDispatch* iface, UINT* n) { *n = 0; return S_OK; }
static HRESULT WINAPI Stub_GetTypeInfo(IDispatch* iface, UINT i, LCID lcid, ITypeInfo** ti) { return E_NOTIMPL; }

static HRESULT WINAPI Stub_GetIDsOfNames(IDispatch* iface, REFIID riid, LPOLESTR* names, UINT count, LCID lcid, DISPID* ids)
{
   StubObject* This = stub_impl(iface);
   for (int i = 0; i < This->count; i++)
      if (!wcscasecmp(This->members[i].name, names[0])) {
         ids[0] = i + 1;
         return S_OK;
      }
   ids[0] = DISPID_UNKNOWN;
   return DISP_E_UNKNOWNNAME;
}

static HRESULT WINAPI Stub_Invoke(IDispatch* iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
   DISPPARAMS* dp, VARIANT* result, EXCEPINFO* ei, UINT* argerr)
{
   StubObject* This = stub_impl(iface);
   if (id < 1 || id > This->count)
      return DISP_E_MEMBERNOTFOUND;
   if (!result || (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)))
      return S_OK;
   VariantInit(result);
   switch (This->members[id - 1].result) {
   case R_EMPTY:
      break;
   case R_ZERO:
      V_VT(result) = VT_I4;
      V_I4(result) = 0;
      break;
   case R_FALSE:
      V_VT(result) = VT_BOOL;
      V_BOOL(result) = VARIANT_FALSE;
      break;
   case R_ARG_AS_STRING:
      if (dp && dp->cArgs >= 1 && SUCCEEDED(VariantChangeType(result, &dp->rgvarg[dp->cArgs - 1], 0, VT_BSTR)))
         break;
      /* fall through */
   case R_EMPTY_STRING:
      V_VT(result) = VT_BSTR;
      V_BSTR(result) = SysAllocString(L"");
      break;
   }
   return S_OK;
}

static IDispatchVtbl StubVtbl = {
   (void*)Any_QueryInterface, Stub_AddRef, Stub_Release, Stub_GetTypeInfoCount, Stub_GetTypeInfo, Stub_GetIDsOfNames, Stub_Invoke
};

static IUnknown* stub_new(const StubMember* members, int count)
{
   StubObject* o = calloc(1, sizeof(StubObject));
   o->IDispatch_iface.lpVtbl = &StubVtbl;
   o->ref = 1;
   o->members = members;
   o->count = count;
   return (IUnknown*)&o->IDispatch_iface;
}

// --- API used by vbs_check / host_hooks -------------------------------------------

void table_host_init(const char* scripts_dir)
{
   snprintf(g_scripts_dir, sizeof(g_scripts_dir), "%s", scripts_dir);
   g_global = any_new(1);
}

void table_host_set_table_dir(const char* dir)
{
   snprintf(g_table_dir, sizeof(g_table_dir), "%s", dir);
}

// A fresh table for the next script (objects from the previous one are leaked on
// purpose, see Any_Release).
void table_host_reset(void)
{
   g_global = any_new(1);
   g_item_count = 0;
   g_faithful = 0;
   g_const_count = 0;
   wcscpy(g_init_sub, L"Table1_Init");
   wcscpy(g_exit_sub, L"Table1_Exit");
}

void table_host_set_user_dir(const char* dir)
{
   swprintf(g_user_dir, sizeof(g_user_dir) / sizeof(WCHAR), L"%s/", dir);
}

IDispatch* table_host_global(void) { return g_global ? &g_global->IDispatch_iface : NULL; }

// ProgIDs the device can create: plugin overrides (SetCOMObjectOverride in
// plugins/*). The host stubs in def.cpp external_create_object follow below.
static const WCHAR* const kDeviceProgIds[] = {
   L"VPinMAME.Controller", L"B2S.Server", L"FlexDMD.FlexDMD", L"UltraDMD.DMDObject",
   L"PinUpPlayer.PinDisplay", L"WMPlayer.OCX",
};

IUnknown* table_host_create_object(const WCHAR* progid)
{
   if (!g_global)
      return NULL;
   if (!wcscasecmp(progid, L"WScript.Shell"))
      return stub_new(kWshMembers, sizeof(kWshMembers) / sizeof(kWshMembers[0]));
   if (!wcscasecmp(progid, L"Shell.Application"))
      return stub_new(kShellAppMembers, sizeof(kShellAppMembers) / sizeof(kShellAppMembers[0]));
   if (!wcscasecmp(progid, L"PUPDMDControl.DMD"))
      return stub_new(kPupDmdMembers, sizeof(kPupDmdMembers) / sizeof(kPupDmdMembers[0]));
   if (!wcscmp(progid, L"System.Collections.ArrayList")) // def.cpp matches this one case-sensitively
      return (IUnknown*)&any_new(0)->IDispatch_iface;
   if (g_faithful) {
      int known = 0;
      for (size_t i = 0; i < sizeof(kDeviceProgIds) / sizeof(kDeviceProgIds[0]); i++)
         if (!wcscasecmp(kDeviceProgIds[i], progid))
            known = 1;
      if (!known)
         return NULL;
   }
   AnyObject* o = any_new(0);
   o->is_pup_player = !wcscasecmp(progid, L"PinUpPlayer.PinDisplay");
   return (IUnknown*)&o->IDispatch_iface;
}
