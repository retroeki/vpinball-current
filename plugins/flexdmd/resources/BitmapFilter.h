#pragma once

#include "Bitmap.h"
#include "common.h"
#include <SDL3/SDL_surface.h>

namespace Flex {

class BitmapFilter
{
public:
   BitmapFilter();
   virtual ~BitmapFilter() { }

   virtual void Filter(Bitmap* pBitmap) = 0;

   // Whether the filter lowers the resolution, the reason to keep full-resolution art for the
   // detail frame (Bitmap::GetDetailSurface)
   virtual bool LowersResolution() const { return false; }
   // Applies the filter to that full-resolution art. False when the filter cannot follow it
   // (its geometry is in filtered pixels), which drops the full-resolution art.
   virtual bool FilterDetail(Bitmap* pBitmap) { return false; }

protected:
   static ColorRGBA32 GetPixel(const SDL_Surface* const surface, int x, int y);
};

}