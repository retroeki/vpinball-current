// license:GPLv3+
// See ScriptPupDmdControl.h for the rationale.

#include "ScriptPupDmdControl.h"

#include <oaidl.h>
#include <oleauto.h>
#include <cstring>
#include <new>

namespace {

// The PuP DMD driver's methods tables call (all no-arg, results unused).
static const wchar_t* const kPupDmdNames[] = {
   L"DMDOpen",
   L"DMDClose",
   L"DMDPuPMirror",
   L"DMDPuPTextMirror",
};

class ScriptPupDmdControl : public IDispatch
{
public:
   ScriptPupDmdControl() : m_ref(1) {}
   virtual ~ScriptPupDmdControl() = default;

   // IUnknown
   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
   {
      if (!ppv) return E_POINTER;
      if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch)) {
         *ppv = static_cast<IDispatch*>(this);
         AddRef();
         return S_OK;
      }
      *ppv = nullptr;
      return E_NOINTERFACE;
   }
   ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
   ULONG STDMETHODCALLTYPE Release() override
   {
      ULONG r = --m_ref;
      if (r == 0) delete this;
      return r;
   }

   // IDispatch
   HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* pctinfo) override
   {
      if (!pctinfo) return E_POINTER;
      *pctinfo = 0;
      return S_OK;
   }

   HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override
   {
      return E_NOTIMPL;
   }

   HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* rgszNames,
                                           UINT cNames, LCID, DISPID* rgDispId) override
   {
      if (!rgszNames || !rgDispId) return E_POINTER;
      for (UINT i = 0; i < cNames; ++i) {
         DISPID dispid = DISPID_UNKNOWN;
         for (size_t n = 0; n < sizeof(kPupDmdNames) / sizeof(kPupDmdNames[0]); ++n) {
            if (wcsicmp(rgszNames[i], kPupDmdNames[n]) == 0) {
               dispid = static_cast<DISPID>(n + 1);
               break;
            }
         }
         rgDispId[i] = dispid;
         if (dispid == DISPID_UNKNOWN)
            return DISP_E_UNKNOWNNAME;
      }
      return S_OK;
   }

   HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID, LCID, WORD,
                                    DISPPARAMS*, VARIANT* pVarResult,
                                    EXCEPINFO*, UINT*) override
   {
      if (dispIdMember < 1 || dispIdMember > static_cast<DISPID>(sizeof(kPupDmdNames) / sizeof(kPupDmdNames[0])))
         return DISP_E_MEMBERNOTFOUND;
      // No DMD hardware to drive: every method is a no-op returning Empty.
      if (pVarResult) {
         VariantInit(pVarResult);
         V_VT(pVarResult) = VT_EMPTY;
      }
      return S_OK;
   }

private:
   ULONG m_ref;
};

}  // namespace

IDispatch* CreateScriptPupDmdControl()
{
   return new (std::nothrow) ScriptPupDmdControl();
}
