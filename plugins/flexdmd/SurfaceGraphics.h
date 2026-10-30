#pragma once

#include "common.h"
#include <SDL3/SDL_surface.h>

namespace Flex {

class Bitmap;

// Draws in scene units onto a surface `scale` times the scene size (the detail frame draws the
// scene at 2x, see FlexDMD::RenderDetail; the regular frame uses 1)
class SurfaceGraphics final {
public:
   SurfaceGraphics(SDL_Surface* pSurface, int scale = 1);
   ~SurfaceGraphics();

   void Clear();
   void SetColor(ColorRGBA32 color, uint8_t alpha = 255);
   void DrawImage(SDL_Surface* pImage, SDL_Rect* pSrcRect, SDL_Rect* pDstRect);
   // Draws the bitmap's full-resolution art when scaled and it has some, its surface otherwise.
   // At scale 1 (the regular frame every consumer gets) this is exactly the previous
   // DrawImage(pBitmap->GetSurface(), nullptr, pDstRect) call; scale 1 also leaves every
   // rectangle untouched, so the regular frame's pixels are as before this change.
   void DrawBitmap(Bitmap* pBitmap, SDL_Rect* pDstRect);
   void FillRectangle(const SDL_Rect& rect);
   void SetClip(const SDL_Rect& rect);
   void ResetClip();
   void TranslateTransform(int x, int y);
   int GetWidth() const { return m_width; }
   int GetHeight() const { return m_height; }
   SDL_Surface* GetSurface() const { return m_pSurface; }

private:
   SDL_Rect ToSurface(const SDL_Rect& rect) const;

   SDL_Surface* m_pSurface;

   int m_scale;
   int m_width;
   int m_height;
   int m_translateX;
   int m_translateY;
   ColorRGBA32 m_color;
   uint8_t m_alpha;
};

}
