#pragma once

#include "plugins/VPXPlugin.h"
#include "AssetSrc.h"
#include <SDL3/SDL_surface.h>

namespace Flex {

class Bitmap final
{
public:
   Bitmap(void* pData, AssetType assetType);
   Bitmap(Bitmap* pCachedBitmap);
   ~Bitmap();

   PSC_IMPLEMENT_REFCOUNT()

   int GetWidth() const;
   int GetHeight() const;
   int GetLength() const;
   int GetFrameDelay(int pos) const;
   int GetFrameCount() const;
   SDL_Surface* GetSurface() const;
   SDL_Surface* GetFrameSurface(int pos) const;
   void SetData(void* pData);
   SDL_Surface* TakeSurface();

   // Full-resolution art of a bitmap whose filters lowered its resolution (dmd=), for the
   // detail frame (FlexDMD::RenderDetail). Null for every other bitmap.
   SDL_Surface* GetDetailSurface() const { return m_pDetail; }
   void SetDetailSurface(SDL_Surface* pDetail);
   // The detail surface resized to w x h, kept for the last size asked
   SDL_Surface* GetScaledDetail(int w, int h);

private:
   AssetType m_assetType;
   void* m_pData;
   SDL_Surface* m_pDetail = nullptr;
   SDL_Surface* m_pScaledDetail = nullptr;
};

}
