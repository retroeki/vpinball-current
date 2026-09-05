#pragma once

#include "objidl.h"
#include "pole/pole.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
using std::string;

class PoleStorage : public IStorage {
public:
   static HRESULT Create(const string& szFilename, const string& szName, IStorage** ppstg);
   // Sub-storage over the SAME parsed document. Replaces the old Clone(), which
   // re-opened the file and re-parsed header/FAT/MiniFAT/directory from scratch.
   static HRESULT Share(PoleStorage* pParent, const string& szName, IStorage** ppstg);

   HRESULT StreamExists(const string& szName);
   POLE::Storage* getPOLEStorage();
   string getPath();

   // POLE has no locking of its own and a StorageIO owns ONE file position, so
   // two threads inside it would interleave their seeks and reads. Every entry
   // into the shared document is serialized on this mutex, which is shared by
   // the whole storage/stream tree. Held by the shim, never inside POLE.
   const std::shared_ptr<std::mutex>& getMutex() const { return m_pMutex; }

   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
   ULONG STDMETHODCALLTYPE AddRef();
   ULONG STDMETHODCALLTYPE Release();

   STDMETHOD(CreateStream)(LPCOLESTR pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream **ppstm);
   STDMETHOD(OpenStream)(LPCOLESTR pwcsName, void *reserved1, DWORD grfMode, DWORD reserved2, IStream **ppstm);
   STDMETHOD(CreateStorage)(LPCOLESTR pwcsName, DWORD grfMode, DWORD dwStgFmt, DWORD reserved2, IStorage **ppstg);
   STDMETHOD(OpenStorage)(LPCOLESTR pwcsName, IStorage *pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage **ppstg);
   STDMETHOD(CopyTo)(DWORD ciidExclude, const IID *rgiidExclude, SNB snbExclude, IStorage *pstgDest);
   STDMETHOD(MoveElementTo)(LPCOLESTR pwcsName, IStorage *pstgDest, LPCOLESTR pwcsNewName, DWORD grfFlags);
   STDMETHOD(Commit)(DWORD grfCommitFlags);
   STDMETHOD(Revert)();
   STDMETHOD(EnumElements)(DWORD reserved1, void *reserved2, DWORD reserved3, IEnumSTATSTG **ppenum);
   STDMETHOD(DestroyElement)(LPCOLESTR pwcsName);
   STDMETHOD(RenameElement)(LPCOLESTR pwcsOldName, LPCOLESTR pwcsNewName);
   STDMETHOD(SetElementTimes)(LPCOLESTR pwcsName, const FILETIME *pctime, const FILETIME *patime, const FILETIME *pmtime);
   STDMETHOD(SetClass)(REFCLSID clsid);
   STDMETHOD(SetStateBits)(DWORD grfStateBits, DWORD grfMask);
   STDMETHOD(Stat)(STATSTG *pstatstg, DWORD grfStatFlag);

private:
  // Shared by the whole tree. A table has thousands of streams and each one
  // used to get its own parsed copy of this.
  POLE::Storage* m_pPOLEStorage = nullptr;
  // Non-null on a sub-storage: holds the owner alive and marks us a borrower,
  // so only the root deletes the document.
  PoleStorage* m_pParent = nullptr;
  std::shared_ptr<std::mutex> m_pMutex;
  string m_szFilename;
  string m_szPath;

  std::atomic<ULONG> m_dwRef { 0 };
};
