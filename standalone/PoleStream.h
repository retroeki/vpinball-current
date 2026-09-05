#pragma once

#include "objidl.h"
#include "pole/pole.h"

#include "PoleStorage.h"

#include <atomic>
#include <memory>
#include <mutex>

class PoleStream : public IStream {
public:
   // Caller MUST already hold the storage mutex: constructing the POLE::Stream
   // resolves the directory entry and walks the FAT, both shared state.
   static HRESULT Create(PoleStorage* pStorage, const string& szName, IStream** ppstm);

   HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);
   ULONG STDMETHODCALLTYPE AddRef();
   ULONG STDMETHODCALLTYPE Release();

   STDMETHOD(Seek)(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition);
   STDMETHOD(SetSize)(ULARGE_INTEGER libNewSize);
   STDMETHOD(CopyTo)(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten);
   STDMETHOD(Commit)(DWORD grfCommitFlags);
   STDMETHOD(Revert)();
   STDMETHOD(LockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
   STDMETHOD(UnlockRegion)(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType);
   STDMETHOD(Stat)(STATSTG *pstatstg, DWORD grfStatFlag);
   STDMETHOD(Clone)(IStream **ppstm);
   STDMETHOD(Read)(void *pv, ULONG cb, ULONG *pcbRead);
   STDMETHOD(Write)(const void *pv, ULONG cb, ULONG *pcbWritten);

private:
   PoleStorage* m_pStorage = nullptr;
   POLE::Stream* m_pPOLEStream = nullptr;
   // Own copy of the storage's lock, so it stays alive while we tear the
   // POLE::Stream down even if we are the last reference to the storage.
   std::shared_ptr<std::mutex> m_pMutex;

   std::atomic<ULONG> m_dwRef { 0 };
};
