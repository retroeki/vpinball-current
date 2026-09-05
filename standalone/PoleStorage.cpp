#include "core/stdafx.h"
#include "PoleStorage.h"
#include "PoleStream.h"

HRESULT PoleStorage::Create(const string& szFilename, const string& szName, IStorage** ppstg)
{
   POLE::Storage* pPOLEStorage = new POLE::Storage(szFilename.c_str());

   if (pPOLEStorage->open() && pPOLEStorage->result() == POLE::Storage::Ok) {
      PoleStorage* pStg = new PoleStorage();
      pStg->m_szFilename = szFilename;
      pStg->m_szPath = szName;
      pStg->m_pPOLEStorage = pPOLEStorage;
      pStg->m_pMutex = std::make_shared<std::mutex>();

      pStg->AddRef();

      *ppstg = pStg;

      return S_OK;
   }

   delete pPOLEStorage;

   return STG_E_FILENOTFOUND;
}

HRESULT PoleStorage::Share(PoleStorage* pParent, const string& szName, IStorage** ppstg)
{
   PoleStorage* pStg = new PoleStorage();
   pStg->m_szFilename = pParent->m_szFilename;
   pStg->m_szPath = szName;
   pStg->m_pPOLEStorage = pParent->m_pPOLEStorage;
   pStg->m_pMutex = pParent->m_pMutex;
   pStg->m_pParent = pParent;

   pParent->AddRef();
   pStg->AddRef();

   *ppstg = pStg;

   return S_OK;
}

HRESULT PoleStorage::StreamExists(const string& szName)
{
   return m_pPOLEStorage->exists(m_szPath + '/' + szName) ? S_OK : STG_E_INVALIDNAME;
}

POLE::Storage* PoleStorage::getPOLEStorage()
{
   return m_pPOLEStorage;
}

string PoleStorage::getPath()
{
   return m_szPath;
}

STDMETHODIMP PoleStorage::QueryInterface(REFIID iid, void **ppvObjOut) { return E_NOTIMPL; }

STDMETHODIMP_(ULONG) PoleStorage::AddRef()
{
   return ++m_dwRef;
}

STDMETHODIMP_(ULONG) PoleStorage::Release()
{
   const ULONG dwRef = --m_dwRef;

   if (dwRef == 0) {
      // Only the root owns the parsed document; a sub-storage just drops the
      // reference that was keeping the root alive.
      if (m_pParent)
         m_pParent->Release();
      else
         delete m_pPOLEStorage;

      delete this;
   }

   return dwRef;
}

STDMETHODIMP PoleStorage::CreateStream(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm) { return E_NOTIMPL; }

STDMETHODIMP PoleStorage::OpenStream(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm)
{
   char szName[1024];
   WideCharToMultiByte(CP_ACP, 0, pwcsName, -1, szName, sizeof(szName), NULL, NULL);

   // Resolving the entry and building the stream's block chain both read the
   // shared document, so they happen under the lock. PoleStream::Create must
   // therefore NOT take it again.
   std::lock_guard<std::mutex> lock(*m_pMutex);

   HRESULT hr;
   if (FAILED(hr = StreamExists(szName)))
      return hr;

   AddRef(); // the stream holds a reference to us for its lifetime
   if (FAILED(hr = PoleStream::Create(this, szName, ppstm)))
      Release();

   return hr;
}

STDMETHODIMP PoleStorage::CreateStorage(LPCOLESTR pwcsName, DWORD grfMode, DWORD dwStgFmt, DWORD reserved2, IStorage **ppstg) { return E_NOTIMPL; }

STDMETHODIMP PoleStorage::OpenStorage(LPCOLESTR pwcsName, IStorage *pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage **ppstg)
{
   char szName[1024];
   WideCharToMultiByte(CP_ACP, 0, pwcsName, -1, szName, sizeof(szName), NULL, NULL);

   // Same document, different path prefix. This used to re-open and re-parse
   // the whole file just to address a sub-storage.
   return Share(this, szName, ppstg);
}

STDMETHODIMP PoleStorage::CopyTo(DWORD ciidExclude, const IID *rgiidExclude, SNB snbExclude, IStorage *pstgDest) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::MoveElementTo(LPCOLESTR pwcsName, IStorage *pstgDest, LPCOLESTR pwcsNewName, DWORD grfFlags) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::Commit(DWORD grfCommitFlags) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::Revert() { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::EnumElements(DWORD reserved1, void *reserved2, DWORD reserved3, IEnumSTATSTG **ppenum) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::DestroyElement(LPCOLESTR pwcsName) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::RenameElement(LPCOLESTR pwcsOldName, LPCOLESTR pwcsNewName) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::SetElementTimes(LPCOLESTR pwcsName, const FILETIME *pctime, const FILETIME *patime, const FILETIME *pmtime) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::SetClass(REFCLSID clsid) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::SetStateBits(DWORD grfStateBits, DWORD grfMask) { return E_NOTIMPL; }
STDMETHODIMP PoleStorage::Stat(STATSTG *pstatstg, DWORD grfStatFlag) { return E_NOTIMPL; }
