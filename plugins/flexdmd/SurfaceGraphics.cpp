#include "SurfaceGraphics.h"
#include "resources/Bitmap.h"

namespace Flex {

SurfaceGraphics::SurfaceGraphics(SDL_Surface* pSurface, int scale)
{
   m_pSurface = pSurface;

   m_scale = scale;
   m_width = pSurface->w / scale;
   m_height = pSurface->h / scale;
   m_color = RGB(0, 0, 0);
   m_alpha = 255;

   m_translateX = 0;
   m_translateY = 0;
}

SurfaceGraphics::~SurfaceGraphics()
{
}

SDL_Rect SurfaceGraphics::ToSurface(const SDL_Rect& rect) const
{
   return { (rect.x + m_translateX) * m_scale, (rect.y + m_translateY) * m_scale, rect.w * m_scale, rect.h * m_scale };
}

void SurfaceGraphics::SetColor(ColorRGBA32 color, uint8_t alpha)
{
   m_color = color;
   m_alpha = alpha;
}

void SurfaceGraphics::Clear()
{
   FillRectangle({ 0, 0, m_width, m_height });
}

void SurfaceGraphics::DrawImage(SDL_Surface* pImage, SDL_Rect* pSrcRect, SDL_Rect* pDstRect)
{
   SDL_Rect rect = ToSurface(*pDstRect);
   SDL_BlitSurfaceScaled(pImage, pSrcRect, m_pSurface, &rect, SDL_SCALEMODE_NEAREST);
}

void SurfaceGraphics::DrawBitmap(Bitmap* pBitmap, SDL_Rect* pDstRect)
{
   if (m_scale > 1 && pBitmap->GetDetailSurface())
   {
      SDL_Rect rect = ToSurface(*pDstRect);
      // Resized once per size and kept, so each frame is a plain copy
      SDL_Surface* const pScaled = pBitmap->GetScaledDetail(rect.w, rect.h);
      if (pScaled)
      {
         SDL_BlitSurface(pScaled, nullptr, m_pSurface, &rect);
         return;
      }
   }
   DrawImage(pBitmap->GetSurface(), nullptr, pDstRect);
}

void SurfaceGraphics::FillRectangle(const SDL_Rect& rect)
{
   SDL_Rect dstRect = ToSurface(rect);

   if (m_alpha == 255)
      SDL_FillSurfaceRect(m_pSurface, &dstRect, SDL_MapSurfaceRGB(m_pSurface, GetRValue(m_color), GetGValue(m_color), GetBValue(m_color)));
   else {
      SDL_Surface* pSource = SDL_CreateSurface(m_pSurface->w, m_pSurface->h, SDL_PIXELFORMAT_RGBA32);
      SDL_FillSurfaceRect(pSource, nullptr, SDL_MapSurfaceRGBA(pSource, GetRValue(m_color), GetGValue(m_color), GetBValue(m_color), m_alpha));
      SDL_BlitSurfaceScaled(pSource, nullptr, m_pSurface, &dstRect, SDL_SCALEMODE_NEAREST);
      SDL_DestroySurface(pSource);
   }
}

void SurfaceGraphics::SetClip(const SDL_Rect& rect)
{
   SDL_Rect dstRect = ToSurface(rect);
   SDL_SetSurfaceClipRect(m_pSurface, &dstRect);
}

void SurfaceGraphics::ResetClip()
{
   SDL_SetSurfaceClipRect(m_pSurface, nullptr);
}

void SurfaceGraphics::TranslateTransform(int x, int y)
{
   m_translateX += x;
   m_translateY += y;
}

}
