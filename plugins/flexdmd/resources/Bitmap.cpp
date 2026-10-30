#include "Bitmap.h"
#include <SDL3_image/SDL_image.h>

namespace Flex {

Bitmap::Bitmap(void* pData, AssetType assetType)
{
   m_pData = pData;
   m_assetType = assetType;
}

Bitmap::Bitmap(Bitmap* pCachedBitmap)
{
   m_pData = (void*)SDL_ConvertSurface(pCachedBitmap->GetSurface(), SDL_PIXELFORMAT_RGBA32);
   m_assetType = pCachedBitmap->m_assetType;
}

Bitmap::~Bitmap()
{
   if (m_pData) {
      if (m_assetType == AssetType_Image)
         SDL_DestroySurface(static_cast<SDL_Surface*>(m_pData));
      else if (m_assetType == AssetType_GIF)
         IMG_FreeAnimation(static_cast<IMG_Animation*>(m_pData));
   }
   SetDetailSurface(nullptr);
}

SDL_Surface* Bitmap::TakeSurface()
{
   SDL_Surface* const pSurface = GetSurface();
   if (pSurface)
      m_pData = nullptr;
   return pSurface;
}

void Bitmap::SetDetailSurface(SDL_Surface* pDetail)
{
   SDL_DestroySurface(m_pScaledDetail);
   m_pScaledDetail = nullptr;
   SDL_DestroySurface(m_pDetail);
   m_pDetail = pDetail;
}

SDL_Surface* Bitmap::GetScaledDetail(int w, int h)
{
   if (m_pDetail == nullptr || w <= 0 || h <= 0)
      return nullptr;
   if (m_pScaledDetail && m_pScaledDetail->w == w && m_pScaledDetail->h == h)
      return m_pScaledDetail;
   SDL_DestroySurface(m_pScaledDetail);
   m_pScaledDetail = SDL_CreateSurface(w, h, m_pDetail->format);
   if (m_pScaledDetail && !SDL_StretchSurface(m_pDetail, nullptr, m_pScaledDetail, nullptr, SDL_SCALEMODE_LINEAR))
   {
      SDL_DestroySurface(m_pScaledDetail);
      m_pScaledDetail = nullptr;
   }
   return m_pScaledDetail;
}

void Bitmap::SetData(void* pData)
{
   if (m_pData) {
      if (m_assetType == AssetType_Image)
         SDL_DestroySurface(static_cast<SDL_Surface*>(m_pData));
      else if (m_assetType == AssetType_GIF)
         IMG_FreeAnimation(static_cast<IMG_Animation*>(m_pData));
   }

   m_pData = pData;
}

SDL_Surface* Bitmap::GetSurface() const
{
   return (m_pData && (m_assetType == AssetType_Image)) ? static_cast<SDL_Surface*>(m_pData) : nullptr;
}

SDL_Surface* Bitmap::GetFrameSurface(int pos) const
{
   return (m_pData && (m_assetType == AssetType_GIF)) ? static_cast<IMG_Animation*>(m_pData)->frames[pos] : nullptr;
}

int Bitmap::GetWidth() const
{
   if (m_pData) {
      if (m_assetType == AssetType_Image)
         return static_cast<SDL_Surface*>(m_pData)->w;
      else if (m_assetType == AssetType_GIF)
         return static_cast<IMG_Animation*>(m_pData)->w;
   }

   return 0;
}

int Bitmap::GetHeight() const
{
   if (m_pData) {
      if (m_assetType == AssetType_Image)
         return static_cast<SDL_Surface*>(m_pData)->h;
      else if (m_assetType == AssetType_GIF)
         return static_cast<IMG_Animation*>(m_pData)->h;
   }

   return 0;
}

int Bitmap::GetLength() const
{
   int length = 0;
   if (m_pData && m_assetType == AssetType_GIF)
   {
      const IMG_Animation* const pAnimation = static_cast<IMG_Animation*>(m_pData);
      for (int index = 0; index < pAnimation->count; index++)
         length += pAnimation->delays[index];
   }
   return length;
}

int Bitmap::GetFrameDelay(int pos) const
{
   if (m_pData && m_assetType == AssetType_GIF)
      return static_cast<IMG_Animation*>(m_pData)->delays[pos];

   return 0;
}

int Bitmap::GetFrameCount() const
{
   if (m_pData && m_assetType == AssetType_GIF)
      return static_cast<IMG_Animation*>(m_pData)->count;

   return 0;
}

}
