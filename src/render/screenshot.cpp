﻿/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/

#include <SpecialK/stdafx.h>
#include <SpecialK/resource.h>

#include <SpecialK/render/backend.h>
#include <SpecialK/render/gl/opengl_screenshot.h>
#include <SpecialK/render/d3d9/d3d9_screenshot.h>
#include <SpecialK/render/d3d11/d3d11_screenshot.h>
#include <SpecialK/render/d3d12/d3d12_screenshot.h>

#include <filesystem>

#ifdef  __SK_SUBSYSTEM__
#undef  __SK_SUBSYSTEM__
#endif
#define __SK_SUBSYSTEM__ L"Screenshot"

SK_Screenshot::framebuffer_s::PinnedBuffer
SK_Screenshot::framebuffer_s::root_;

SK_ScreenshotQueue::MemoryTotals SK_ScreenshotQueue::pooled;
SK_ScreenshotQueue::MemoryTotals SK_ScreenshotQueue::completed;

SK_ScreenshotQueue      enqueued_screenshots { 0,   0,  0,  0,  0 };
SK_ScreenshotQueue      enqueued_sounds      { 0,   0,  0,  0,  0 };
SK_ScreenshotTitleQueue enqueued_titles      { "", "", "", "", "" };

void SK_Screenshot_PlaySound (void)
{
  if (config.screenshots.play_sound)
  {
    static const auto sound_file =
      std::filesystem::path (SK_GetInstallPath ()) /
           LR"(Assets\Shared\Sounds\screenshot.wav)";

    std::error_code                          ec;
    if (std::filesystem::exists (sound_file, ec))
    {
      SK_PlaySound ( sound_file.c_str (),
                       nullptr, SND_ASYNC |
                                SND_FILENAME );
    }
  }
}

void SK_Screenshot_ProcessQueue ( SK_ScreenshotStage stage,
                            const SK_RenderBackend&  rb = SK_GetCurrentRenderBackend () )
{
  if ( sk::narrow_cast <int> (rb.api) &
       sk::narrow_cast <int> (SK_RenderAPI::D3D12) )
  {
    SK_D3D12_ProcessScreenshotQueue (stage);
  }

  else
  if ( sk::narrow_cast <int> (rb.api) &
       sk::narrow_cast <int> (SK_RenderAPI::D3D11) )
  {
    SK_D3D11_ProcessScreenshotQueue (stage);
  }

  else
  if ( sk::narrow_cast <int> (rb.api) &
       sk::narrow_cast <int> (SK_RenderAPI::D3D9) )
  {
    SK_D3D9_ProcessScreenshotQueue (stage);
  }

  else
  if ( sk::narrow_cast <int> (rb.api) &
       sk::narrow_cast <int> (SK_RenderAPI::OpenGL) )
  {
    SK_GL_ProcessScreenshotQueue (stage);
  }
}

bool
SK_Screenshot_IsCapturingHUDless (void)
{
  if (ReadAcquire (&__SK_ScreenShot_CapturingHUDless))
  {
    return true;
  }

  if ( ReadAcquire (&enqueued_screenshots.pre_game_hud)   > 0 ||
       ReadAcquire (&enqueued_screenshots.without_sk_osd) > 0 )
  {
    return true;
  }

  return false;
}

bool
SK_Screenshot_IsCapturing (void)
{
  return
    ( ReadAcquire (&enqueued_screenshots.stages [0]) +
      ReadAcquire (&enqueued_screenshots.stages [1]) +
      ReadAcquire (&enqueued_screenshots.stages [2]) +
      ReadAcquire (&enqueued_screenshots.stages [3]) +
      ReadAcquire (&enqueued_screenshots.stages [4]) ) > 0;
}

void
SK_ScreenshotManager::init (void) noexcept
{
  __try {
    const auto& repo =
      getRepoStats (true);

    std::ignore = repo;
  }

  __except (EXCEPTION_EXECUTE_HANDLER) {
    // Don't care
  };
}

#include <filesystem>

const wchar_t*
SK_ScreenshotManager::getBasePath (void) const
{
  static wchar_t
    wszAbsolutePathToScreenshots [ MAX_PATH + 2 ] = { };

  if (*wszAbsolutePathToScreenshots != L'\0')
    return wszAbsolutePathToScreenshots;

  if (! config.screenshots.override_path.empty ())
  {
    std::error_code                                                ec;
    if (std::filesystem::exists (config.screenshots.override_path, ec))
    {
      auto path_to_app =
        SK_GetFullyQualifiedApp ();

      std::wstring profile_path =
        std::wstring (LR"(\)") + SK_GetHostApp () + std::wstring (LR"(\XXX)");

      if (! SK_IsInjected ())
      {
        if (app_cache_mgr->loadAppCacheForExe       (path_to_app))
        {
          auto path =
            app_cache_mgr->getConfigPathFromAppPath (path_to_app);

          if (! path._Equal (SK_GetNaiveConfigPath ()))
            profile_path = path;
        }
      }

      else
        profile_path = SK_GetConfigPath ();

      const wchar_t *wszProfileName =
        std::filesystem::path (profile_path).parent_path ().c_str ();

      while (StrStrIW (wszProfileName, LR"(\)") != nullptr)
                       wszProfileName = StrStrIW (wszProfileName, LR"(\)") + 1;

      auto override_path =
        std::filesystem::path (config.screenshots.override_path) / wszProfileName;

      wcsncpy_s    ( wszAbsolutePathToScreenshots, MAX_PATH,
                     override_path.c_str (),       _TRUNCATE );
      wcscat       ( wszAbsolutePathToScreenshots, L"\\"     );
      SK_LOGi0 ( L"@ Using Custom Screenshot Path: %ws",
                     wszAbsolutePathToScreenshots );
      return         wszAbsolutePathToScreenshots;
    }
  }

  wcsncpy_s    ( wszAbsolutePathToScreenshots, MAX_PATH,
                 SK_GetConfigPath (),          _TRUNCATE        );
  PathAppendW  ( wszAbsolutePathToScreenshots, L"Screenshots\\" );

  return         wszAbsolutePathToScreenshots;
}

bool
SK_HDR_ConvertImageToPNG (const DirectX::Image& raw_hdr_img, DirectX::ScratchImage& png_img)
{
  using namespace DirectX;

  if (auto typeless_fmt = DirectX::MakeTypeless (raw_hdr_img.format);
           typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS  ||
           typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
           typeless_fmt == DXGI_FORMAT_R32G32B32A32_TYPELESS)
  {
    if (png_img.GetImageCount () == 0)
    {
      if (FAILED (png_img.Initialize2D (DXGI_FORMAT_R16G16B16A16_UNORM,
              raw_hdr_img.width,
              raw_hdr_img.height, 1, 1)))
      {
        return false;
      }
    }

    if (png_img.GetMetadata ().format != DXGI_FORMAT_R16G16B16A16_UNORM)
      return false;

    uint16_t* rgb16_pixels =
      reinterpret_cast <uint16_t *> (png_img.GetPixels ());

    if (rgb16_pixels == nullptr)
      return false;

    struct ParamsPQ
    {
      XMVECTOR N, M;
      XMVECTOR C1, C2, C3;
      XMVECTOR MaxPQ;
    };

    static const ParamsPQ PQ =
    {
      XMVectorReplicate (2610.0 / 4096.0 / 4.0),   // N
      XMVectorReplicate (2523.0 / 4096.0 * 128.0), // M
      XMVectorReplicate (3424.0 / 4096.0),         // C1
      XMVectorReplicate (2413.0 / 4096.0 * 32.0),  // C2
      XMVectorReplicate (2392.0 / 4096.0 * 32.0),  // C3
      XMVectorReplicate (125.0),
    };

    static const XMMATRIX c_from709to2020 =
    {
      0.627225305694944f,  0.0690418812810714f, 0.0163911702607078f, 0.0f,
      0.329476882715808f,  0.919605681354755f,  0.0880887513437058f, 0.0f,
      0.0432978115892484f, 0.0113524373641739f, 0.895520078395586f,  0.0f,
      0.0f,                0.0f,                0.0f,                1.0f
    };

    auto LinearToPQ = [](XMVECTOR N)
    {
      XMVECTOR ret;
      XMVECTOR sign =
        XMVectorSet (XMVectorGetX (N) < 0.0f ? -1.0f : 1.0f,
                     XMVectorGetY (N) < 0.0f ? -1.0f : 1.0f,
                     XMVectorGetZ (N) < 0.0f ? -1.0f : 1.0f, 1.0f);

      ret =
        XMVectorPow (XMVectorDivide (XMVectorAbs (N), PQ.MaxPQ), PQ.N);

      XMVECTOR nd =
        XMVectorDivide (
           XMVectorAdd (  PQ.C1, XMVectorMultiply (PQ.C2, ret)),
           XMVectorAdd (g_XMOne, XMVectorMultiply (PQ.C3, ret))
        );

      return
        XMVectorMultiply (XMVectorPow (nd, PQ.M), sign);
    };

    EvaluateImage ( raw_hdr_img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      static const XMVECTOR pq_range_10bpc = XMVectorReplicate (1023.0f),
                            pq_range_12bpc = XMVectorReplicate (4095.0f),
                            pq_range_16bpc = XMVectorReplicate (65535.0f),
                            pq_range_32bpc = XMVectorReplicate (4294967295.0f);

      auto pq_range_out =
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? pq_range_10bpc :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? pq_range_12bpc :
                                                              pq_range_12bpc;//pq_range_16bpc;

      pq_range_out = pq_range_16bpc;

      const auto pq_range_in  =
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? pq_range_10bpc :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? pq_range_16bpc :
                                                              pq_range_32bpc;

      int intermediate_bits = 16;
      int output_bits       = 
        (typeless_fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  ? 10 :
        (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) ? 12 :
                                                              12;//16;

      output_bits = 16;

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v =
          *pixels++;

        // Assume scRGB for any FP32 input, though uncommon
        if (typeless_fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
            typeless_fmt == DXGI_FORMAT_R32G32B32A32_TYPELESS)
        {
          XMVECTOR nvalue = XMVector3Transform (v, c_from709to2020);
                        v = LinearToPQ (XMVectorClamp (nvalue, g_XMZero, g_XMInfinity));
        }

        v = // Quantize to 10- or 12-bpc before expanding to 16-bpc in order to improve
          XMVectorRound ( // compression efficiency
            XMVectorMultiply (
              XMVectorSaturate (v), pq_range_out));

        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetX (v)) << (intermediate_bits - output_bits);
        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetY (v)) << (intermediate_bits - output_bits);
        *(rgb16_pixels++) =
          static_cast <uint16_t> (DirectX::XMVectorGetZ (v)) << (intermediate_bits - output_bits);
          rgb16_pixels++; // We have an unused alpha channel that needs skipping
      }
    });
  }

  return true;
}

bool
SK_HDR_SavePNGToDisk (const wchar_t* wszPNGPath, const DirectX::Image* png_image,
                                                 const DirectX::Image* raw_image,
                         const char* szUtf8MetadataTitle)
{
  if ( wszPNGPath == nullptr ||
        png_image == nullptr ||
        raw_image == nullptr )
  {
    return false;
  }

  std::string metadata_title (
         szUtf8MetadataTitle != nullptr ?
         szUtf8MetadataTitle            :
         "HDR10 PNG" );

  if (SUCCEEDED (
    DirectX::SaveToWICFile (*png_image, DirectX::WIC_FLAGS_NONE,
                           GetWICCodec (DirectX::WIC_CODEC_PNG),
                               wszPNGPath, &GUID_WICPixelFormat48bppRGB,
                                              SK_WIC_SetMaximumQuality,
                                            [&](IWICMetadataQueryWriter *pMQW)
                                            {
                                              SK_WIC_SetMetadataTitle (pMQW, metadata_title);
                                            })))
  {
    return
      SK_PNG_MakeHDR (wszPNGPath, *png_image, *raw_image);
  }

  return false;
}

// The parameters are screwy here because currently the only successful way
//   of doing this copy involves passing the path to a file, but the intention
//     is actually to pass raw image data and transfer it using OLE.
bool
SK_PNG_CopyToClipboard (const DirectX::Image& image, const void *pData, size_t data_size)
{
  std::ignore = image;

  SK_ReleaseAssert (data_size <= DWORD_MAX);

  if (OpenClipboard (nullptr))
  {
    int clpSize = sizeof (DROPFILES);

    clpSize += sizeof (wchar_t) * static_cast <int> (wcslen ((wchar_t *)pData) + 1); // + 1 => '\0'
    clpSize += sizeof (wchar_t);                                                     // two \0 needed at the end

    HDROP hdrop =
      (HDROP)GlobalAlloc (GHND, clpSize);

    DROPFILES* df =
      (DROPFILES *)GlobalLock (hdrop);

    df->pFiles = sizeof (DROPFILES);
    df->fWide  = TRUE;

    wcscpy ((wchar_t*)&df [1], (const wchar_t *)pData);

    GlobalUnlock     (hdrop);
    EmptyClipboard   ();
    SetClipboardData (CF_HDROP, hdrop);
    CloseClipboard   ();

    return true;
  }

  return false;
}

bool
SK_ScreenshotManager::copyToClipboard ( const DirectX::Image& image,
                                        const DirectX::Image* pOptionalHDR,
                                        const wchar_t*        wszOptionalFilename ) const
{
  if (! config.screenshots.copy_to_clipboard)
    return false;

  if (pOptionalHDR != nullptr && config.screenshots.allow_hdr_clipboard)
  {
    auto snip = 
      getSnipRect ();

    const DirectX::Image *pImg = pOptionalHDR;
    DirectX::ScratchImage sub_img;

    if (snip.w != 0 && snip.h != 0)
    {
      if (SUCCEEDED (sub_img.Initialize2D (pImg->format, snip.w, snip.h, 1, 1)))
      {
        if (SUCCEEDED (DirectX::CopyRectangle (*pOptionalHDR, snip, *sub_img.GetImage (0,0,0), 0, 0, 0)))
        {
          pImg =
            sub_img.GetImages ();

          SK_GetCurrentRenderBackend ().screenshot_mgr->setSnipRect ({0,0,0,0});
        }
      }
    }


    DirectX::ScratchImage                hdr10_img;
    if (SK_HDR_ConvertImageToPNG (*pImg, hdr10_img))
    {
      wchar_t                   wszPNGPath [MAX_PATH + 2] = { };
      if (                                  wszOptionalFilename != nullptr)
      { wcsncpy_s (             wszPNGPath, wszOptionalFilename, MAX_PATH);
        PathRemoveExtensionW   (wszPNGPath);
        PathRemoveFileSpecW    (wszPNGPath);
      } else {
        wcsncpy_s              (wszPNGPath, getBasePath (), MAX_PATH);
      }
      PathAppendW              (wszPNGPath, L"hdr10_clipboard");
      PathAddExtensionW        (wszPNGPath, L".png");
      if (SK_HDR_SavePNGToDisk (wszPNGPath, hdr10_img.GetImages (), pOptionalHDR))
      {
        if (SK_PNG_CopyToClipboard (*hdr10_img.GetImage (0,0,0), wszPNGPath, 0))
        {
          return true;
        }
      }
    }
  }

  if (OpenClipboard (nullptr))
  {
    auto snip = 
      getSnipRect ();

    const DirectX::Image *pImg = &image;
    DirectX::ScratchImage sub_img;

    if (snip.w != 0 && snip.h != 0)
    {
      if (SUCCEEDED (sub_img.Initialize2D (pImg->format, snip.w, snip.h, 1, 1)))
      {
        if (SUCCEEDED (DirectX::CopyRectangle (image, snip, *sub_img.GetImage (0,0,0), 0, 0, 0)))
        {
          pImg =
            sub_img.GetImages ();

          SK_GetCurrentRenderBackend ().screenshot_mgr->setSnipRect ({0,0,0,0});
        }
      }
    }

    const int
        _bpc    =
      sk::narrow_cast <int> (DirectX::BitsPerPixel (pImg->format)),
        _width  =
      sk::narrow_cast <int> (                       pImg->width),
        _height =
      sk::narrow_cast <int> (                       pImg->height);

    SK_ReleaseAssert (pImg->format == DXGI_FORMAT_B8G8R8X8_UNORM ||
                      pImg->format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                      pImg->format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);

    HBITMAP hBitmapCopy =
       CreateBitmap (
         _width, _height, 1,
           _bpc, pImg->pixels
       );

    BITMAPINFOHEADER
      bmh                 = { };
      bmh.biSize          = sizeof (BITMAPINFOHEADER);
      bmh.biWidth         =   _width;
      bmh.biHeight        =  -_height;
      bmh.biPlanes        =  1;
      bmh.biBitCount      = sk::narrow_cast <WORD> (_bpc);
      bmh.biCompression   = BI_RGB;
      bmh.biXPelsPerMeter = 10;
      bmh.biYPelsPerMeter = 10;

    BITMAPINFO
      bmi                 = { };
      bmi.bmiHeader       = bmh;

    HDC hdcDIB =
      CreateCompatibleDC (GetDC (nullptr));

    void* bitplane = nullptr;

    HBITMAP
      hBitmap =
        CreateDIBSection ( hdcDIB, &bmi, DIB_RGB_COLORS,
            &bitplane, nullptr, 0 );
    memcpy ( bitplane,
               pImg->pixels,
        static_cast <size_t> (_bpc / 8) *
        static_cast <size_t> (_width  ) *
        static_cast <size_t> (_height )
           );

    HDC hdcSrc = CreateCompatibleDC (GetDC (nullptr));
    HDC hdcDst = CreateCompatibleDC (GetDC (nullptr));

    if ( hBitmap    != nullptr &&
        hBitmapCopy != nullptr )
    {
      auto hbmpSrc = (HBITMAP)SelectObject (hdcSrc, hBitmap);
      auto hbmpDst = (HBITMAP)SelectObject (hdcDst, hBitmapCopy);

      BitBlt (hdcDst, 0, 0, _width,
                            _height, hdcSrc, 0, 0, SRCCOPY);

      SelectObject     (hdcSrc, hbmpSrc);
      SelectObject     (hdcDst, hbmpDst);

      EmptyClipboard   ();
      SetClipboardData (CF_BITMAP, hBitmapCopy);
    }

    CloseClipboard   ();

    DeleteDC         (hdcSrc);
    DeleteDC         (hdcDst);
    DeleteDC         (hdcDIB);

    if ( hBitmap     != nullptr &&
         hBitmapCopy != nullptr )
    {
      DeleteBitmap   (hBitmap);
      DeleteBitmap   (hBitmapCopy);

      return true;
    }

    return false;
  }

  return false;
}

bool
SK_ScreenshotManager::checkDiskSpace (uint64_t bytes_needed) const
{
  ULARGE_INTEGER
    useable  = { }, // Amount the current user's quota allows
    capacity = { },
    free     = { };

  SK_CreateDirectoriesEx (getBasePath (), false);

  const BOOL bRet =
    GetDiskFreeSpaceExW (
      getBasePath (),
        &useable, &capacity, &free );

  if (! bRet)
  {
    SK_ImGui_Warning (
      SK_FormatStringW (
        L"GetDiskFreeSpaceExW (%ws) Failed\r\n\r\n"
        L"\t>> Reason: %ws",
          getBasePath (),
          _com_error (
            HRESULT_FROM_WIN32 (GetLastError ())
          ).ErrorMessage ()
      ).c_str ()
    );

    return false;
  }

  // Don't take screenshots if the storage for one single screenshot is > 85% of remaining space
  if (static_cast <double> (bytes_needed) > static_cast <double> (useable.QuadPart) * .85)
  {
    SK_ImGui_Warning (L"Not enough Disk Space to take Screenshot, please free up or disable feature.");
    return false;
  }

  // Don't take screenshots if the general free space is < .25%
  if (static_cast <double> (free.QuadPart) / static_cast <double> (capacity.QuadPart) < 0.0025)
  {
    SK_ImGui_Warning (L"Free space on Screenshot Drive is < 0.25%, disabling Screenshots.");
    return false;
  }

  return true;
}

SK_ScreenshotManager::screenshot_repository_s&
SK_ScreenshotManager::getRepoStats (bool refresh)
{
  static auto _ConstructPath =
  [](auto&& path_base, const wchar_t* path_end)
  {
    std::array <wchar_t, MAX_PATH + 1>
                     path { };
    SK_PathCombineW (path.data (), path_base.data (), path_end);
    return           path;
  };

  static bool init = false;

  if (refresh || (! init))
  {
    screenshots.files           = 0;
    screenshots.liSize.QuadPart = 0ULL;

    WIN32_FIND_DATA fd        = {   };
    std::wstring    directory = getBasePath ();
    HANDLE          hFind     =
      FindFirstFileW   (_ConstructPath (directory, L"*").data (), &fd);

    if (hFind != INVALID_HANDLE_VALUE)
    {
      do
      {
        if (         (fd.dwFileAttributes  & FILE_ATTRIBUTE_DIRECTORY) ==
                                             FILE_ATTRIBUTE_DIRECTORY  &&
          (  wcsncmp (fd.cFileName, L"." , MAX_PATH) != 0)             &&
            (wcsncmp (fd.cFileName, L"..", MAX_PATH) != 0) )
        {
          unsigned int
          SK_Steam_RecursiveFileScrub ( std::wstring   directory,
                                        unsigned int  &files,
                                        LARGE_INTEGER &liSize,
                                  const wchar_t       *wszPattern,
                                  const wchar_t       *wszAntiPattern,     // Exact match only
                                        bool           erase = false );

          SK_Steam_RecursiveFileScrub ( _ConstructPath   (
                                          _ConstructPath ( directory, fd.cFileName ),
                                          L""            ).data (),
                                          screenshots.files,
                                          screenshots.liSize,
            L"*",                       L"SK_SteamScreenshotImport.jpg",
            false                     );
        }
      } while (FindNextFile (hFind, &fd));

      FindClose (hFind);

      init = true;
    }
  }

  return screenshots;
}

void
SK_TriggerHudFreeScreenshot (void) noexcept
{
  if (SK_API_IsLayeredOnD3D11 (SK_GetCurrentRenderBackend ().api))
  {
    if (ReadAcquire (&SK_D3D11_TrackingCount->Conditional) > 0)
    {
      InterlockedIncrement (&__SK_D3D11_QueuedShots);
    }
  }

  else if (SK_GetCurrentRenderBackend ().api == SK_RenderAPI::D3D12)
  {
    //if (ReadAcquire (&SK_D3D11_TrackingCount->Conditional) > 0)
    //{
      InterlockedIncrement (&__SK_D3D12_QueuedShots);
    //}
  }
}

void
SK_Screenshot::sanitizeFilename (bool allow_subdirs)
{
  const wchar_t *wszInvalidFileChars =
    allow_subdirs ?   LR"(:*?"<>|&!.	)"
                  : LR"(\/:*?"<>|&!.	)";

  static const
    std::unordered_map <wchar_t, wchar_t> fs_safe_alt_chars =
    {
      { L'\\',L'⧵' }, { L'/', L'⧸'  }, { L':', L'ː'  },
      { L'|', L'｜'}, { L'<', L'﹤' }, { L'>', L'﹥' },
      { L'"', L'“' }, { L'?', L'﹖' }, { L'*', L'﹡' },
      { L'.', L'․' }, { L'	', L' ' }, { L'&', L'＆' },
      { L'!', L'！' }
    };

   size_t start = 0; // Remove (replace with _) invalid filesystem characters
  while ((start = framebuffer.file_name.find_first_of (wszInvalidFileChars, start)) != std::wstring::npos)
  {
    if (fs_safe_alt_chars.contains (framebuffer.file_name [start]))
    {                               framebuffer.file_name [start] =
              fs_safe_alt_chars.at (framebuffer.file_name [start]);
    } else {                        framebuffer.file_name [start] = L'_'; }

    ++start;
  }

          start = 0; // Consolidate all runs of multiple _'s into a single _
  while ((start = framebuffer.file_name.find (L"__", start)) != std::wstring::npos)
  {
    framebuffer.file_name.replace (start++, 2, L"_");
  }

          start = 0; // If we have _ followed by a space, transform it to a single space
  while ((start = framebuffer.file_name.find (L"_ ", start)) != std::wstring::npos)
  {
    framebuffer.file_name.replace (start++, 2, L" ");
  }
}

SK_Screenshot::SK_Screenshot (bool clipboard_only)
{
  bPlaySound       = config.screenshots.play_sound;
  bCopyToClipboard = config.screenshots.copy_to_clipboard;

  if (clipboard_only)
  {
    bCopyToClipboard = true;
    bSaveToDisk      = false;
  }

  else
    bSaveToDisk = true;

  framebuffer.AllowCopyToClipboard = bCopyToClipboard;
  framebuffer.AllowSaveToDisk      = bSaveToDisk;

  std::wstring format_str = config.screenshots.filename_format;

  const std::wstring wide_name =
    SK_UTF8ToWideChar (SK_GetFriendlyAppName ());

  size_t  start = 0;
  while ((start = format_str.find (L"%G", start)) != std::wstring::npos)
  {
    format_str.replace (start, 2, wide_name.c_str ());
                        start  += wide_name.length ();
  }

  wchar_t   name [MAX_PATH] = { };   time_t   now;      time (&now);
  wcsftime (name, MAX_PATH-1, format_str.c_str (), localtime (&now));

  framebuffer.file_name = name;

  sanitizeFilename ();
}

#include <../depends/include/DirectXTex/d3dx12.h>
#include <../depends/include/avif/avif.h>

using namespace DirectX;

using avifImageCreate_pfn            = avifImage*  (*)(uint32_t width, uint32_t height, uint32_t depth, avifPixelFormat yuvFormat);
using avifRGBImageSetDefaults_pfn    = void        (*)(avifRGBImage* rgb, const avifImage* image);
using avifRGBImageAllocatePixels_pfn = avifResult  (*)(avifRGBImage* rgb);
using avifImageRGBToYUV_pfn          = avifResult  (*)(avifImage* image, const avifRGBImage* rgb);
using avifEncoderCreate_pfn          = avifEncoder*(*)(void);
using avifEncoderAddImage_pfn        = avifResult  (*)(avifEncoder* encoder, const avifImage* image, uint64_t durationInTimescales, avifAddImageFlags addImageFlags);
using avifEncoderFinish_pfn          = avifResult  (*)(avifEncoder* encoder, avifRWData* output);
using avifImageDestroy_pfn           = void        (*)(avifImage* image);
using avifEncoderDestroy_pfn         = void        (*)(avifEncoder* encoder);
using avifRGBImageFreePixels_pfn     = void        (*)(avifRGBImage* rgb);

static avifImageCreate_pfn            SK_avifImageCreate            = nullptr;
static avifRGBImageSetDefaults_pfn    SK_avifRGBImageSetDefaults    = nullptr;
static avifRGBImageAllocatePixels_pfn SK_avifRGBImageAllocatePixels = nullptr;
static avifImageRGBToYUV_pfn          SK_avifImageRGBToYUV          = nullptr;
static avifEncoderCreate_pfn          SK_avifEncoderCreate          = nullptr;
static avifEncoderAddImage_pfn        SK_avifEncoderAddImage        = nullptr;
static avifEncoderFinish_pfn          SK_avifEncoderFinish          = nullptr;
static avifImageDestroy_pfn           SK_avifImageDestroy           = nullptr;
static avifEncoderDestroy_pfn         SK_avifEncoderDestroy         = nullptr;
static avifRGBImageFreePixels_pfn     SK_avifRGBImageFreePixels     = nullptr;

bool
SK_Screenshot_SaveAVIF (DirectX::ScratchImage &src_image, const wchar_t *wszFilePath, uint16_t max_cll, uint16_t max_pall)
{
  static bool init = false;

  SK_RunOnce (
  {
    std::filesystem::path avif_dll =
      SK_GetPlugInDirectory (SK_PlugIn_Type::ThirdParty);

    avif_dll /=
      SK_RunLHIfBitness ( 64, LR"(Image Codecs\libavif\libavif_x64.dll)",
                              LR"(Image Codecs\libavif\libavif_x86.dll)" );

    HMODULE hModAVIF =
      SK_LoadLibraryW (avif_dll.c_str ());

    if (hModAVIF != 0)
    {
      SK_avifImageCreate            = (avifImageCreate_pfn)           SK_GetProcAddress (hModAVIF, "avifImageCreate");
      SK_avifRGBImageSetDefaults    = (avifRGBImageSetDefaults_pfn)   SK_GetProcAddress (hModAVIF, "avifRGBImageSetDefaults");
      SK_avifRGBImageAllocatePixels = (avifRGBImageAllocatePixels_pfn)SK_GetProcAddress (hModAVIF, "avifRGBImageAllocatePixels");
      SK_avifImageRGBToYUV          = (avifImageRGBToYUV_pfn)         SK_GetProcAddress (hModAVIF, "avifImageRGBToYUV");
      SK_avifEncoderCreate          = (avifEncoderCreate_pfn)         SK_GetProcAddress (hModAVIF, "avifEncoderCreate");
      SK_avifEncoderAddImage        = (avifEncoderAddImage_pfn)       SK_GetProcAddress (hModAVIF, "avifEncoderAddImage");
      SK_avifEncoderFinish          = (avifEncoderFinish_pfn)         SK_GetProcAddress (hModAVIF, "avifEncoderFinish");
      SK_avifImageDestroy           = (avifImageDestroy_pfn)          SK_GetProcAddress (hModAVIF, "avifImageDestroy");
      SK_avifEncoderDestroy         = (avifEncoderDestroy_pfn)        SK_GetProcAddress (hModAVIF, "avifEncoderDestroy");
      SK_avifRGBImageFreePixels     = (avifRGBImageFreePixels_pfn)    SK_GetProcAddress (hModAVIF, "avifRGBImageFreePixels");

      init =
        ( SK_avifImageCreate            != nullptr &&
          SK_avifRGBImageSetDefaults    != nullptr &&
          SK_avifRGBImageAllocatePixels != nullptr &&
          SK_avifImageRGBToYUV          != nullptr &&
          SK_avifEncoderCreate          != nullptr &&
          SK_avifEncoderAddImage        != nullptr &&
          SK_avifEncoderFinish          != nullptr &&
          SK_avifImageDestroy           != nullptr &&
          SK_avifEncoderDestroy         != nullptr &&
          SK_avifRGBImageFreePixels     != nullptr );
    }
  });

  if (! init)
    return false;

  auto& metadata =
    src_image.GetMetadata ();

  uint32_t width  = sk::narrow_cast <uint32_t> (metadata.width);
  uint32_t height = sk::narrow_cast <uint32_t> (metadata.height);

  int             bit_depth  = 10;
  avifPixelFormat yuv_format = AVIF_PIXEL_FORMAT_YUV444;

  switch (config.screenshots.avif.yuv_subsampling)
  {
    default:
      config.screenshots.avif.yuv_subsampling = 444; // Write a valid value to INI
      [[fallthrough]];
    case 444:
      yuv_format = AVIF_PIXEL_FORMAT_YUV444;
      break;
    case 422:
      yuv_format = AVIF_PIXEL_FORMAT_YUV422;
      break;
    case 420:
      yuv_format = AVIF_PIXEL_FORMAT_YUV420;
      break;
    case 400: // lol
      yuv_format = AVIF_PIXEL_FORMAT_YUV400;
      break;
  }

  if (metadata.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
  {
    bit_depth =
      std::clamp (config.screenshots.avif.scrgb_bit_depth, 8, 12);

    // 8, 10, 12... nothing else.
    if ( bit_depth == 9 ||
         bit_depth == 11 )
         bit_depth++;

    // Write a valid value back to INI
    config.screenshots.avif.scrgb_bit_depth = bit_depth;
  }

  avifResult rgbToYuvResult = AVIF_RESULT_NO_CONTENT;
  avifResult addResult      = AVIF_RESULT_NO_CONTENT;
  avifResult encodeResult   = AVIF_RESULT_NO_CONTENT;

  avifRWData   avifOutput = AVIF_DATA_EMPTY;
  avifRGBImage rgb        = { };
  avifEncoder* encoder    = nullptr;
  avifImage*   image      =
    SK_avifImageCreate (width, height, bit_depth, yuv_format);

  if (image != nullptr)
  {
    image->yuvRange =
      config.screenshots.avif.full_range ? AVIF_RANGE_FULL
                                         : AVIF_RANGE_LIMITED;

    switch (metadata.format)
    {
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
        image->colorPrimaries          = AVIF_COLOR_PRIMARIES_BT2020;
        image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
        image->matrixCoefficients      = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;
        break;
      default:
        return false;
    }

    SK_avifRGBImageSetDefaults (&rgb, image);

    rgb.depth       = metadata.format == DXGI_FORMAT_R10G10B10A2_UNORM ? 10 : 16;
    rgb.ignoreAlpha = true;
    rgb.isFloat     = false;
    rgb.format      = AVIF_RGB_FORMAT_RGB;

    SK_avifRGBImageAllocatePixels (&rgb);

    switch (metadata.format)
    {
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      {
        uint16_t* rgb_pixels = (uint16_t *)rgb.pixels;

        EvaluateImage ( src_image.GetImages     (),
                        src_image.GetImageCount (),
                        metadata,
        [&](const DirectX::XMVECTOR* pixels, size_t width, size_t y)
        {
          UNREFERENCED_PARAMETER(y);

          for (size_t j = 0; j < width; ++j)
          {
            DirectX::XMVECTOR v = *pixels++;

            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetX (v) * 1023.0f));
            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetY (v) * 1023.0f));
            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetZ (v) * 1023.0f));
          }
        } );
      } break;

      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      {
        struct ParamsPQ
        {
          XMVECTOR N, M;
          XMVECTOR C1, C2, C3;
          XMVECTOR MaxPQ;
        };

        static const ParamsPQ PQ =
        {
          XMVectorReplicate (2610.0 / 4096.0 / 4.0),   // N
          XMVectorReplicate (2523.0 / 4096.0 * 128.0), // M
          XMVectorReplicate (3424.0 / 4096.0),         // C1
          XMVectorReplicate (2413.0 / 4096.0 * 32.0),  // C2
          XMVectorReplicate (2392.0 / 4096.0 * 32.0),  // C3
          XMVectorReplicate (125.0),
        };

        static const XMMATRIX c_from709to2020 = // Transposed
        {
          0.627225305694944f,  0.0690418812810714f, 0.0163911702607078f, 0.0f,
          0.329476882715808f,  0.919605681354755f,  0.0880887513437058f, 0.0f,
          0.0432978115892484f, 0.0113524373641739f, 0.895520078395586f,  0.0f,
          0.0f,                0.0f,                0.0f,                1.0f
        };

        auto LinearToPQ = [](XMVECTOR N)
        {
          XMVECTOR ret;

          ret =
            XMVectorPow (N, PQ.N);

          XMVECTOR nd =
            XMVectorDivide (
               XMVectorAdd (  PQ.C1, XMVectorMultiply (PQ.C2, ret)),
               XMVectorAdd (g_XMOne, XMVectorMultiply (PQ.C3, ret))
            );

          return
            XMVectorPow (nd, PQ.M);
        };

        uint16_t* rgb_pixels = (uint16_t *)rgb.pixels;

        DirectX::ScratchImage
          hdr10_img;
          hdr10_img.InitializeFromImage (*src_image.GetImages ());


        EvaluateImage ( src_image.GetImages     (),
                        src_image.GetImageCount (),
                        metadata,
        [&](_In_reads_ (width) const XMVECTOR* pixels, size_t width, size_t y)
        {
          UNREFERENCED_PARAMETER (y);

          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR value = pixels [j];

            value = XMVector3Transform (XMVectorDivide   (value, PQ.MaxPQ), c_from709to2020);
            value =         LinearToPQ (XMVectorSaturate (value));

            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetX (value) * 65535.0f));
            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetY (value) * 65535.0f));
            *(rgb_pixels++) = static_cast <uint16_t> (roundf (XMVectorGetZ (value) * 65535.0f));
          }
        } );
      } break;
    }

    rgbToYuvResult =
      SK_avifImageRGBToYUV (image, &rgb);
  }

  if (rgbToYuvResult == AVIF_RESULT_OK)
  {
    encoder =
      SK_avifEncoderCreate ();

    if (encoder != nullptr)
    {
      encoder->quality         = config.screenshots.compression_quality;
      encoder->qualityAlpha    = config.screenshots.compression_quality; // N/A?
      encoder->timescale       = 1;
      encoder->repetitionCount = AVIF_REPETITION_COUNT_INFINITE;
      encoder->maxThreads      = config.screenshots.avif.max_threads * 4;
      encoder->speed           = config.screenshots.avif.compression_speed;
      encoder->minQuantizer    = AVIF_QUANTIZER_BEST_QUALITY;
      encoder->maxQuantizer    = AVIF_QUANTIZER_BEST_QUALITY;
      encoder->codecChoice     = AVIF_CODEC_CHOICE_AUTO;

      image->clli.maxCLL  = max_cll;
      image->clli.maxPALL = max_pall;

      addResult    = SK_avifEncoderAddImage (encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
      encodeResult = SK_avifEncoderFinish   (encoder, &avifOutput);
    }
  }

  if ( rgbToYuvResult != AVIF_RESULT_OK ||
       addResult      != AVIF_RESULT_OK ||
       encodeResult   != AVIF_RESULT_OK )
  {
    SK_LOGi0 (L"rgbToYUV: %d, addImage: %d, encode: %d", rgbToYuvResult, addResult, encodeResult);
  }

  if (encodeResult == AVIF_RESULT_OK)
  {
    wchar_t    wszAVIFPath [MAX_PATH + 2] = { };
    wcsncpy_s (wszAVIFPath, wszFilePath, MAX_PATH);

    PathRemoveExtensionW (wszAVIFPath);
    PathAddExtensionW    (wszAVIFPath, L".avif");

    FILE* fAVIF =
      _wfopen (wszAVIFPath, L"wb");

    if (fAVIF != nullptr)
    {
      fwrite (avifOutput.data, 1, avifOutput.size, fAVIF);
      fclose (fAVIF);
    }
  }

  if (image   != nullptr) SK_avifImageDestroy   (image);
  if (encoder != nullptr) SK_avifEncoderDestroy (encoder);

  SK_avifRGBImageFreePixels (&rgb);

  return
    ( encodeResult == AVIF_RESULT_OK );
}

void
SK_WIC_SetMaximumQuality (IPropertyBag2 *props)
{
  if (props == nullptr)
    return;

  PROPBAG2 opt = {   .pstrName = L"ImageQuality"   };
  VARIANT  var = { VT_R4,0,0,0, { .fltVal = 1.0f } };

  PROPBAG2 opt2 = { .pstrName = L"FilterOption"                    };
  VARIANT  var2 = { VT_UI1,0,0,0, { .bVal = WICPngFilterAdaptive } };

  props->Write (1, &opt,  &var);
  props->Write (1, &opt2, &var2);
}

void
SK_WIC_SetMetadataTitle (IWICMetadataQueryWriter *pMQW, std::string& title)
{
  SK_WIC_SetBasicMetadata (pMQW);

  if (! title.empty ())
  {
    PROPVARIANT       value;
    PropVariantInit (&value);

    value.vt     = VT_LPSTR;
    value.pszVal = const_cast <char *> (title.c_str ());

    pMQW->SetMetadataByName (
      L"System.Title",
      &value
    );
  }
}

void
SK_WIC_SetBasicMetadata (IWICMetadataQueryWriter *pMQW)
{
  PROPVARIANT       value;
  PropVariantInit (&value);

  std::string app_name =
    SK_GetFriendlyAppName ();

  value.vt     = VT_LPSTR;
  value.pszVal = const_cast <char *> (app_name.c_str ());

  pMQW->SetMetadataByName (
    L"System.ApplicationName",
    &value
  );

  UINT SK_DPI_Update (void);
  UINT dpi =
    SK_DPI_Update ();

  value.vt     = VT_R8;
  value.dblVal = static_cast <double> (dpi);

  pMQW->SetMetadataByName (
    L"System.Image.HorizontalResolution",
    &value
  );

  pMQW->SetMetadataByName (
    L"System.Image.VerticalResolution",
    &value
  );;

  value.vt     = VT_LPSTR;
  value.pszVal = const_cast <char *> (
    SK_FormatString ("Captured using Special K %hs", SK_VersionStrA).c_str ()
  );

  pMQW->SetMetadataByName (
    L"System.Comment",
    &value
  );

  value.vt = VT_DATE;

  SYSTEMTIME      st;
  GetSystemTime (&st);

  SystemTimeToVariantTime (&st, &value.dblVal);

  pMQW->SetMetadataByName (
    L"System.Photo.DateTaken",
    &value
  );

  if (config.screenshots.embed_nickname)
  {
    static char                          szName [512] = { };
    SK_RunOnce (SK_Platform_GetUserName (szName, 511));

    if (*szName != '\0')
    {
      char* names [] = { szName };

      value.vt             = VT_VECTOR | VT_LPSTR;
      value.calpstr.cElems = 1;
      value.calpstr.pElems = names;

      pMQW->SetMetadataByName (
        L"System.Author",
        &value
      );
    }
  }
}


uint32_t
png_crc32 (const void* typeless_data, size_t offset, size_t len, uint32_t crc)
{
  auto data =
    (const BYTE *)typeless_data;

  uint32_t c;

  static uint32_t
      png_crc_table [256] = { };
  if (png_crc_table [ 0 ] == 0)
  {
    for (auto i = 0 ; i < 256 ; ++i)
    {
      c = i;

      for (auto j = 0 ; j < 8 ; ++j)
      {
        if ((c & 1) == 1)
          c = (0xEDB88320 ^ ((c >> 1) & 0x7FFFFFFF));
        else
          c = ((c >> 1) & 0x7FFFFFFF);
      }

      png_crc_table [i] = c;
    }
  }

  c =
    (crc ^ 0xffffffff);

  for (auto k = offset ; k < (offset + len) ; ++k)
  {
    c =
      png_crc_table [(c ^ data [k]) & 255] ^
                    ((c >> 8)       & 0xFFFFFF);
  }
  
  return
    (c ^ 0xffffffff);
}

/* This is for compression type. PNG 1.0-1.2 only define the single type. */
constexpr uint8_t PNG_COMPRESSION_TYPE_BASE = 0; /* Deflate method 8, 32K window */
#define PNG_COMPRESSION_TYPE_DEFAULT PNG_COMPRESSION_TYPE_BASE

//
// To convert an image passed to an encoder that does not understand HDR,
//   but that we actually fed HDR pixels to... perform the following:
//
//  1. Remove gAMA chunk  (Prevents SKIV from recognizing as HDR)
//  2. Remove sRGB chunk  (Prevents Discord from rendering in HDR)
//
//  3. Add cICP  (The primary way of defining HDR10) 
//  4. Add iCCP  (Required for Discord to render in HDR)
//
//  (5) Add cLLi  [Unnecessary, but probably a good idea]
//  (6) Add cHRM  [Unnecessary, but probably a good idea]
//

#if (defined _M_IX86) || (defined _M_X64)
# define SK_PNG_GetUint32(x)                    _byteswap_ulong (x)
# define SK_PNG_SetUint32(x,y)              x = _byteswap_ulong (y)
# define SK_PNG_DeclareUint32(x,y) uint32_t x = SK_PNG_SetUint32((x),(y))
#else
# define SK_PNG_GetUint32(x)                    (x)
# define SK_PNG_SetUint32(x,y)              x = (y)
# define SK_PNG_DeclareUint32(x,y) uint32_t x = SK_PNG_SetUint32((x),(y))
#endif

struct SK_PNG_HDR_cHRM_Payload
{
  SK_PNG_DeclareUint32 (white_x, 31270);
  SK_PNG_DeclareUint32 (white_y, 32900);
  SK_PNG_DeclareUint32 (red_x,   70800);
  SK_PNG_DeclareUint32 (red_y,   29200);
  SK_PNG_DeclareUint32 (green_x, 17000);
  SK_PNG_DeclareUint32 (green_y, 79700);
  SK_PNG_DeclareUint32 (blue_x,  13100);
  SK_PNG_DeclareUint32 (blue_y,  04600);
};

struct SK_PNG_HDR_sBIT_Payload
{
  uint8_t red_bits   = 10; // 12 if source was scRGB (compression optimization)
  uint8_t green_bits = 10; // 12 if source was scRGB (compression optimization)
  uint8_t blue_bits  = 10; // 12 if source was scRGB (compression optimization)
};

struct SK_PNG_HDR_mDCv_Payload
{
  struct {
    SK_PNG_DeclareUint32 (red_x,   35400); // 0.708 / 0.00002
    SK_PNG_DeclareUint32 (red_y,   14600); // 0.292 / 0.00002
    SK_PNG_DeclareUint32 (green_x,  8500); // 0.17  / 0.00002
    SK_PNG_DeclareUint32 (green_y, 39850); // 0.797 / 0.00002
    SK_PNG_DeclareUint32 (blue_x,   6550); // 0.131 / 0.00002
    SK_PNG_DeclareUint32 (blue_y,   2300); // 0.046 / 0.00002
  } primaries;

  struct {
    SK_PNG_DeclareUint32 (x, 15635); // 0.3127 / 0.00002
    SK_PNG_DeclareUint32 (y, 16450); // 0.3290 / 0.00002
  } white_point;

  // The only real data we need to fill-in
  struct {
    SK_PNG_DeclareUint32 (maximum, 10000000); // 1000.0 cd/m^2
    SK_PNG_DeclareUint32 (minimum, 1);        // 0.0001 cd/m^2
  } luminance;
};

struct SK_PNG_HDR_cLLi_Payload
{
  SK_PNG_DeclareUint32 (max_cll,  10000000); // 1000 / 0.0001
  SK_PNG_DeclareUint32 (max_fall,  2500000); //  250 / 0.0001
};

//
// ICC Profile for tonemapping comes courtesy of ledoge
//
//   https://github.com/ledoge/jxr_to_png
//
struct SK_PNG_HDR_iCCP_Payload
{
  char          profile_name [20]   = "RGB_D65_202_Rel_PeQ";
  uint8_t       compression_type    = PNG_COMPRESSION_TYPE_DEFAULT;
  unsigned char profile_data [2178] = {
	0x78, 0x9C, 0xED, 0x97, 0x79, 0x58, 0x13, 0x67, 0x1E, 0xC7, 0x47, 0x50,
	0x59, 0x95, 0x2A, 0xAC, 0xED, 0xB6, 0x8B, 0xA8, 0x54, 0x20, 0x20, 0x42,
	0xE5, 0xF4, 0x00, 0x51, 0x40, 0x05, 0xAF, 0x6A, 0x04, 0x51, 0x6E, 0x84,
	0x70, 0xAF, 0x20, 0x24, 0xDC, 0x87, 0x0C, 0xA8, 0x88, 0x20, 0x09, 0x90,
	0x04, 0x12, 0x24, 0x24, 0x90, 0x03, 0x82, 0xA0, 0x41, 0x08, 0x24, 0x41,
	0x2E, 0x21, 0x01, 0x12, 0x83, 0x4A, 0x10, 0xA9, 0x56, 0xB7, 0x8A, 0xE0,
	0xAD, 0x21, 0xE0, 0xB1, 0x6B, 0x31, 0x3B, 0x49, 0x74, 0x09, 0x6D, 0xD7,
	0x3E, 0xCF, 0x3E, 0xFD, 0xAF, 0x4E, 0x3E, 0xF3, 0xBC, 0xBF, 0x79, 0xBF,
	0xEF, 0xBC, 0x33, 0x9F, 0xC9, 0xFC, 0x31, 0x2F, 0x00, 0xE8, 0xBC, 0x8D,
	0x4A, 0x3E, 0x62, 0x30, 0xD7, 0x09, 0x00, 0xA2, 0x63, 0xE2, 0x91, 0xEE,
	0x6E, 0x2E, 0x06, 0x7B, 0x82, 0x82, 0x0D, 0xB4, 0x46, 0x01, 0x6D, 0x60,
	0x0E, 0xA0, 0xDC, 0x82, 0x10, 0xA8, 0x58, 0x67, 0x38, 0x7C, 0x8F, 0xEA,
	0xE8, 0x57, 0x1B, 0x34, 0xEA, 0xF5, 0xB0, 0x6A, 0xAC, 0xC4, 0x42, 0x31,
	0xD7, 0xF2, 0x84, 0x9D, 0x68, 0xBD, 0xA9, 0xD6, 0x43, 0xEB, 0x16, 0xE5,
	0xBD, 0xFC, 0xC6, 0xD2, 0xFC, 0xF8, 0xFF, 0x38, 0xEF, 0xE3, 0xB6, 0x30,
	0x24, 0x14, 0x85, 0x80, 0xDA, 0x9F, 0xA1, 0x7D, 0x1B, 0x22, 0x16, 0x19,
	0x0F, 0x4D, 0xE9, 0x04, 0xD5, 0x46, 0x49, 0xF1, 0xB1, 0x8A, 0x3A, 0x04,
	0xAA, 0xBF, 0x44, 0x44, 0x04, 0x41, 0xED, 0x9C, 0x64, 0xA8, 0x36, 0x47,
	0x44, 0x22, 0x62, 0xA1, 0x9A, 0x06, 0xD5, 0xDA, 0x48, 0x2F, 0x6F, 0x1F,
	0xA8, 0x66, 0x29, 0xC6, 0x84, 0xAB, 0xEA, 0x1E, 0x45, 0x1D, 0xAC, 0xAA,
	0x47, 0x14, 0xB5, 0xB3, 0xB5, 0x8B, 0x25, 0x54, 0x3F, 0x03, 0x80, 0xC5,
	0x97, 0x5C, 0xAC, 0x9D, 0xA1, 0x5A, 0xA7, 0x06, 0xEA, 0x87, 0x47, 0x1F,
	0x49, 0x50, 0x5C, 0xF7, 0x83, 0x03, 0xA0, 0x1D, 0x1A, 0xE3, 0xE9, 0x01,
	0xB5, 0x30, 0x68, 0xD7, 0x07, 0xDC, 0x01, 0x37, 0xC0, 0x05, 0x08, 0x04,
	0xB6, 0x01, 0xEB, 0x00, 0x3B, 0xA8, 0xB5, 0x06, 0x2C, 0xA1, 0x3D, 0x10,
	0xEA, 0x0F, 0x05, 0x8E, 0x40, 0x2D, 0x1C, 0x6A, 0xF7, 0x43, 0xCF, 0xEC,
	0xB7, 0xE7, 0x98, 0xAF, 0x9C, 0x63, 0x2B, 0xF4, 0x83, 0xAE, 0x06, 0xDD,
	0x8A, 0x81, 0x6A, 0xC8, 0xCC, 0x73, 0x42, 0x85, 0xD9, 0x58, 0xAB, 0xCE,
	0xD2, 0x86, 0x5C, 0xE7, 0xDD, 0x91, 0xCB, 0x27, 0xCD, 0x00, 0x40, 0xAB,
	0x18, 0x00, 0xA6, 0x0B, 0xE5, 0xF2, 0x77, 0x54, 0xB9, 0x7C, 0x9A, 0x0A,
	0x00, 0x9A, 0xB7, 0x01, 0xA0, 0x33, 0x4B, 0xE5, 0x0B, 0x00, 0x0B, 0x74,
	0x80, 0x39, 0x33, 0x73, 0xD5, 0x45, 0x00, 0x80, 0xDB, 0x51, 0xB9, 0x5C,
	0x9E, 0x3D, 0xD3, 0x67, 0x16, 0x09, 0xF5, 0x8F, 0x42, 0xF3, 0xD4, 0xCF,
	0xF4, 0x19, 0x68, 0x01, 0xC0, 0xA2, 0xF3, 0x00, 0x70, 0x65, 0x69, 0x74,
	0x58, 0xBC, 0x95, 0xA2, 0x47, 0x53, 0x73, 0x81, 0xEA, 0x6E, 0x7F, 0xF1,
	0x2F, 0xFE, 0xEA, 0x78, 0x8E, 0x86, 0xE6, 0xDC, 0x79, 0xF3, 0xB5, 0xFE,
	0xB2, 0x60, 0xE1, 0x22, 0xED, 0x2F, 0x16, 0x2F, 0xD1, 0xD1, 0xFD, 0xEB,
	0xD2, 0x2F, 0xBF, 0xFA, 0xDB, 0xD7, 0xDF, 0xFC, 0x5D, 0x6F, 0x99, 0xFE,
	0xF2, 0x15, 0x2B, 0x0D, 0xBE, 0x5D, 0x65, 0x68, 0x64, 0x0C, 0x33, 0x31,
	0x5D, 0x6D, 0xB6, 0xC6, 0xDC, 0xE2, 0xBB, 0xB5, 0x96, 0x56, 0xD6, 0x36,
	0xB6, 0x76, 0xEB, 0xD6, 0x6F, 0xD8, 0x68, 0xEF, 0xB0, 0xC9, 0x71, 0xF3,
	0x16, 0x27, 0x67, 0x97, 0xAD, 0xDB, 0xB6, 0xBB, 0xBA, 0xED, 0xD8, 0xB9,
	0x6B, 0xF7, 0x9E, 0xEF, 0xF7, 0xEE, 0x83, 0xEF, 0x77, 0xF7, 0x38, 0xE0,
	0x79, 0xF0, 0x10, 0x74, 0x6F, 0xBE, 0x7E, 0xFE, 0x01, 0x81, 0x87, 0x83,
	0x82, 0x11, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xFF, 0x38, 0x12,
	0x1D, 0x73, 0x34, 0x36, 0x0E, 0x89, 0x8A, 0x4F, 0x48, 0x4C, 0x4A, 0x4E,
	0x49, 0x4D, 0x4B, 0xCF, 0x38, 0x96, 0x09, 0x66, 0x65, 0x1F, 0x3F, 0x71,
	0x32, 0xE7, 0x54, 0xEE, 0xE9, 0xBC, 0xFC, 0x33, 0x05, 0x68, 0x4C, 0x61,
	0x51, 0x31, 0x16, 0x87, 0x2F, 0x29, 0x25, 0x10, 0xCB, 0xCE, 0x96, 0x93,
	0x2A, 0xC8, 0x94, 0xCA, 0x2A, 0x2A, 0x8D, 0xCE, 0xA8, 0xAE, 0x61, 0xD6,
	0x9E, 0xAB, 0xAB, 0x3F, 0x7F, 0x81, 0xD5, 0x70, 0xB1, 0xB1, 0x89, 0xDD,
	0xDC, 0xC2, 0xE1, 0xF2, 0x5A, 0x2F, 0xB5, 0xB5, 0x77, 0x74, 0x76, 0x5D,
	0xEE, 0xEE, 0xE1, 0x0B, 0x7A, 0xFB, 0xFA, 0x85, 0xA2, 0x2B, 0xE2, 0x81,
	0xAB, 0xD7, 0xAE, 0x0F, 0x4A, 0x86, 0x6E, 0x0C, 0xDF, 0x1C, 0xF9, 0xE1,
	0xD6, 0xED, 0x1F, 0xEF, 0xDC, 0xFD, 0xE7, 0x4F, 0xF7, 0xEE, 0x8F, 0x3E,
	0x18, 0x1B, 0x7F, 0xF8, 0xE8, 0xF1, 0x93, 0xA7, 0xCF, 0x9E, 0xBF, 0x78,
	0x29, 0x9D, 0x90, 0x4D, 0x4E, 0xBD, 0x7A, 0xFD, 0xE6, 0xED, 0xBF, 0xFE,
	0xFD, 0xEE, 0xE7, 0xE9, 0xF7, 0xF2, 0xCF, 0xFE, 0x7F, 0x72, 0x7F, 0x10,
	0x04, 0xB2, 0x32, 0x34, 0x4E, 0x85, 0x2F, 0xAC, 0x70, 0x33, 0x64, 0x1B,
	0xEF, 0xE8, 0x99, 0x1F, 0x7A, 0x41, 0x2F, 0xAE, 0x66, 0x55, 0x22, 0x1D,
	0x36, 0x37, 0x2D, 0x7B, 0x6E, 0x7A, 0xE6, 0xFC, 0xEC, 0xC8, 0xC5, 0xC4,
	0x5D, 0xC6, 0x17, 0x4D, 0x76, 0x76, 0x6B, 0x41, 0x11, 0x52, 0x19, 0xAD,
	0xF0, 0xC1, 0xAE, 0xF4, 0x2D, 0x34, 0x08, 0x4E, 0x35, 0x4E, 0xF3, 0xB6,
	0x25, 0x59, 0xC1, 0xB9, 0xDA, 0x11, 0x75, 0xFA, 0xC8, 0x6A, 0xC3, 0x24,
	0x3A, 0x6C, 0xDF, 0x3A, 0xD6, 0xBE, 0xF5, 0x75, 0x70, 0x07, 0x82, 0xFB,
	0x9E, 0x44, 0xAF, 0xA3, 0x3B, 0x23, 0xCE, 0xEA, 0xC7, 0x51, 0x57, 0x25,
	0xD2, 0x8C, 0x93, 0x69, 0x26, 0xE8, 0x25, 0x62, 0xF4, 0x12, 0x21, 0x5A,
	0xF7, 0x12, 0x46, 0x8F, 0x5C, 0x64, 0x15, 0x87, 0x0F, 0xB1, 0xCF, 0x43,
	0xDB, 0x80, 0xE5, 0xE6, 0x69, 0x95, 0xAB, 0xF9, 0xC0, 0x03, 0x3E, 0x30,
	0xCA, 0x07, 0x7E, 0xE4, 0x03, 0x7D, 0x02, 0x80, 0xD6, 0xAB, 0x17, 0xCD,
	0x8E, 0xD9, 0x57, 0x96, 0xE7, 0x98, 0x43, 0xB4, 0x1C, 0x33, 0x6C, 0x52,
	0xD2, 0x38, 0x66, 0xD8, 0x30, 0x66, 0x54, 0x3B, 0x6E, 0x4A, 0x78, 0x68,
	0x1D, 0xDB, 0x83, 0xF2, 0x26, 0xE7, 0x39, 0x49, 0xF7, 0x13, 0x3F, 0x42,
	0x90, 0xEE, 0x2F, 0x95, 0xBA, 0xE3, 0xA5, 0x07, 0xCE, 0xC8, 0x7C, 0x93,
	0xFA, 0xE2, 0xFD, 0x64, 0xDE, 0xB8, 0x59, 0xF8, 0x60, 0x65, 0x3E, 0x45,
	0x93, 0x7E, 0x79, 0xAF, 0x10, 0x29, 0x1A, 0x27, 0xB2, 0x34, 0x4E, 0x1E,
	0x9B, 0x9B, 0x1F, 0xA1, 0x5D, 0xB9, 0xC3, 0xA8, 0x19, 0xB6, 0x83, 0xAF,
	0xF0, 0x52, 0x29, 0xCF, 0xCB, 0x3C, 0x3E, 0x0F, 0x04, 0xB5, 0x72, 0xA2,
	0x74, 0xCA, 0x77, 0xC3, 0x2E, 0x9A, 0xAA, 0x2B, 0xAF, 0x0C, 0xC4, 0x19,
	0x1C, 0x2E, 0xFA, 0x36, 0x3C, 0x0D, 0x96, 0xE1, 0x63, 0x57, 0x61, 0x05,
	0xE7, 0xA9, 0x29, 0x6F, 0x64, 0xED, 0xB3, 0xAF, 0x87, 0x6F, 0x26, 0xB8,
	0xEF, 0x4D, 0xF2, 0x8E, 0x9D, 0xAD, 0xAC, 0x23, 0x46, 0xEB, 0x0A, 0xD1,
	0x4B, 0xDB, 0x30, 0xCB, 0xC8, 0x45, 0xD6, 0xC8, 0x12, 0xA5, 0x72, 0x56,
	0xB9, 0x79, 0xFA, 0x27, 0x94, 0xCB, 0xFE, 0x78, 0xE5, 0x2F, 0x2A, 0x4E,
	0x2F, 0x26, 0xE7, 0x2C, 0xA9, 0x8A, 0xFD, 0xBA, 0x16, 0xBE, 0x86, 0xBB,
	0x66, 0xB7, 0x60, 0x41, 0x18, 0x6B, 0x19, 0xB2, 0xC6, 0x10, 0xF2, 0xD2,
	0x25, 0xE6, 0xEB, 0x96, 0xE5, 0x2E, 0x2D, 0x47, 0x2E, 0xA3, 0xBB, 0x5B,
	0x34, 0x9B, 0xEF, 0xE1, 0x2F, 0x08, 0xBB, 0xF0, 0x21, 0x32, 0x49, 0x27,
	0x9A, 0xA4, 0x97, 0x98, 0x82, 0xA0, 0xC5, 0x99, 0x80, 0x8D, 0x34, 0x5B,
	0x8F, 0xD6, 0xC5, 0x91, 0xF5, 0xFA, 0x28, 0xA5, 0xB2, 0xFB, 0xF7, 0x17,
	0xDD, 0xF7, 0x5E, 0xF0, 0xD8, 0x5F, 0xE6, 0xE9, 0x97, 0xE2, 0x9B, 0xB4,
	0x3B, 0xAA, 0x62, 0x39, 0x92, 0x66, 0x98, 0x48, 0x83, 0x41, 0xCA, 0x18,
	0xBD, 0x01, 0xCC, 0x32, 0x11, 0x46, 0xBF, 0xAD, 0xD0, 0x88, 0x52, 0xBC,
	0x01, 0x59, 0x12, 0xEE, 0x90, 0x8F, 0x81, 0x94, 0x2D, 0xD4, 0x94, 0xEF,
	0xF0, 0x81, 0x7E, 0xC1, 0x1C, 0x5A, 0xEF, 0xF2, 0x98, 0xE6, 0xA3, 0xF0,
	0xDF, 0x50, 0x36, 0x3E, 0xF7, 0x69, 0xE5, 0x09, 0xCF, 0xDF, 0x57, 0xB6,
	0x6C, 0xAE, 0xB4, 0x6A, 0xAE, 0xB0, 0x6A, 0x39, 0x65, 0xC7, 0x0B, 0x71,
	0xEA, 0xDE, 0x78, 0x48, 0xA4, 0x1B, 0xD5, 0xB0, 0x1C, 0x55, 0x63, 0x94,
	0xC4, 0x80, 0x59, 0x37, 0x56, 0x59, 0x37, 0x91, 0x6D, 0x9A, 0x72, 0xD7,
	0x73, 0x42, 0x9D, 0x2F, 0xDB, 0x7B, 0x09, 0xA1, 0x68, 0x85, 0x2A, 0x72,
	0xA4, 0x32, 0x37, 0x53, 0xE9, 0x9B, 0xE9, 0x18, 0x67, 0xE6, 0x91, 0x5D,
	0x6C, 0x27, 0xFF, 0xCB, 0x5F, 0x45, 0x9F, 0x5F, 0x19, 0x0F, 0x45, 0x74,
	0x58, 0x40, 0x0A, 0x2F, 0x20, 0xA5, 0x25, 0x20, 0xAD, 0xEA, 0x30, 0x08,
	0x86, 0x62, 0xDC, 0x15, 0x2F, 0x0C, 0xC3, 0x18, 0xEA, 0x87, 0x94, 0xB1,
	0x0E, 0xD7, 0xB1, 0x0E, 0x03, 0xD8, 0x4D, 0x5D, 0x38, 0x67, 0x6A, 0x09,
	0x3C, 0xB1, 0x0C, 0xE5, 0x58, 0x50, 0x64, 0x97, 0x4D, 0xB2, 0x48, 0xAF,
	0x5A, 0x2D, 0xD0, 0x18, 0x13, 0x68, 0x3C, 0x10, 0x68, 0xDE, 0xED, 0x9D,
	0x2F, 0xEC, 0x5D, 0x42, 0xEF, 0x33, 0x3D, 0xDA, 0x12, 0x07, 0x2F, 0xCB,
	0xDF, 0x7C, 0x0A, 0x52, 0x36, 0x66, 0x8F, 0x19, 0x37, 0x29, 0xB9, 0x38,
	0x0E, 0x3B, 0x37, 0x6E, 0x46, 0x7C, 0x64, 0xAB, 0x52, 0x76, 0x9E, 0xA5,
	0xEC, 0xFE, 0x51, 0xD9, 0x2F, 0xA9, 0x2F, 0x61, 0xB6, 0xB2, 0xCF, 0x2C,
	0xE5, 0xE0, 0xA1, 0xEE, 0xE0, 0xA1, 0xCE, 0xE0, 0x21, 0x66, 0xC8, 0xF0,
	0x89, 0xC8, 0x5B, 0x9E, 0xF1, 0x23, 0x46, 0x49, 0x6C, 0x58, 0x72, 0xAD,
	0x49, 0x32, 0xC3, 0x04, 0x21, 0xE9, 0x41, 0x48, 0x3A, 0x11, 0x12, 0x66,
	0xE8, 0x8D, 0x93, 0x51, 0x3F, 0x78, 0x26, 0x8C, 0x18, 0xFF, 0x37, 0x8A,
	0x10, 0x09, 0x22, 0x44, 0xDD, 0x11, 0x57, 0xEA, 0xA2, 0xC4, 0xB9, 0x31,
	0x83, 0x5E, 0xC9, 0x83, 0x26, 0x29, 0x8D, 0x26, 0x29, 0x4C, 0x93, 0x14,
	0x86, 0x49, 0x1A, 0xEB, 0x6A, 0x1A, 0x4B, 0x94, 0xD6, 0xD0, 0x9C, 0xDE,
	0x88, 0xCD, 0xE4, 0x86, 0xE4, 0x74, 0x5A, 0x82, 0x75, 0xE6, 0xE9, 0x8C,
	0xD5, 0xA9, 0x74, 0x53, 0x72, 0xE2, 0x6D, 0x72, 0xE2, 0x08, 0x39, 0x51,
	0x48, 0x49, 0xA9, 0xAF, 0x04, 0x41, 0x3A, 0x76, 0x3B, 0x8E, 0x68, 0x9F,
	0x53, 0x61, 0x99, 0x51, 0x65, 0x26, 0x34, 0x7F, 0x2C, 0x34, 0x7F, 0x24,
	0x34, 0xBF, 0x27, 0xFC, 0x4E, 0x2C, 0xB4, 0x65, 0x8A, 0x5C, 0x51, 0xBC,
	0x64, 0x0F, 0x52, 0xC1, 0x96, 0xDC, 0x32, 0xAB, 0x87, 0x6B, 0x5A, 0x3E,
	0xC2, 0x7E, 0x68, 0x7E, 0xFE, 0xE1, 0xDA, 0xB3, 0x8F, 0x37, 0xC4, 0xF1,
	0x13, 0x7C, 0x28, 0xF9, 0xCE, 0x52, 0x0F, 0xA2, 0x1A, 0x04, 0xE9, 0x01,
	0xFC, 0xC4, 0xC1, 0x82, 0x49, 0xFF, 0x64, 0x85, 0xB2, 0x42, 0x53, 0x1D,
	0xAC, 0xCC, 0xB7, 0x68, 0xD2, 0x5F, 0xA1, 0x4C, 0x92, 0x8A, 0x49, 0x52,
	0x11, 0x49, 0x7A, 0x99, 0x24, 0xAD, 0x25, 0x4F, 0x66, 0xD1, 0xDE, 0x6D,
	0xC7, 0x76, 0x6C, 0x3C, 0x59, 0xBF, 0x36, 0xA3, 0xDA, 0x8C, 0xF4, 0x52,
	0x4C, 0x7A, 0x79, 0x85, 0xF4, 0xF2, 0x72, 0x85, 0x32, 0xA2, 0xAB, 0x45,
	0xE4, 0x67, 0x57, 0xC9, 0xCF, 0xC4, 0xE4, 0xE7, 0x3D, 0xE4, 0xE7, 0x75,
	0x95, 0xD2, 0x6C, 0xC6, 0x5B, 0x57, 0x5C, 0xBB, 0xFD, 0xC9, 0x7A, 0x4B,
	0x28, 0x62, 0xDC, 0xBB, 0xC5, 0xB8, 0x37, 0xC2, 0xB8, 0x2F, 0xAE, 0x1E,
	0x6D, 0xAC, 0x19, 0xCF, 0xAD, 0x7B, 0xB1, 0x9B, 0xC8, 0x71, 0xCC, 0xAD,
	0xB5, 0x3A, 0xC6, 0x58, 0xC3, 0x69, 0x93, 0x72, 0xDA, 0x5E, 0x70, 0xDA,
	0x7E, 0xE2, 0xB4, 0x77, 0x73, 0x3B, 0x09, 0xAD, 0x7D, 0xFE, 0xD5, 0x4C,
	0xD7, 0x42, 0xEA, 0xFA, 0x6C, 0xAA, 0x85, 0x04, 0x25, 0x93, 0xA0, 0x26,
	0x24, 0xA8, 0x27, 0x92, 0xF8, 0x9B, 0x92, 0xA4, 0xA6, 0x21, 0x10, 0xEC,
	0xC9, 0xF7, 0xA6, 0x61, 0xB7, 0xE5, 0x97, 0xDB, 0x3E, 0x71, 0xE8, 0x51,
	0xD2, 0xFD, 0x64, 0x53, 0xD7, 0x53, 0x47, 0xCE, 0xD3, 0x2D, 0xD4, 0x67,
	0xAE, 0xA8, 0xFE, 0x34, 0xBF, 0xAA, 0x82, 0xAD, 0x13, 0x07, 0x89, 0x33,
	0x1C, 0x22, 0x4C, 0x1C, 0xC2, 0xCB, 0x7C, 0x0A, 0xA6, 0x0E, 0x27, 0xF7,
	0x27, 0xF9, 0xCB, 0x7C, 0x71, 0xEA, 0x4C, 0xFA, 0x62, 0x27, 0xFD, 0x8A,
	0x26, 0x03, 0xF2, 0x5E, 0x85, 0xA4, 0x70, 0x06, 0x28, 0x9C, 0x01, 0xB2,
	0x92, 0x72, 0xEE, 0x00, 0xAE, 0xF5, 0x6A, 0x66, 0x97, 0xC4, 0xAB, 0x92,
	0xED, 0x92, 0x57, 0x6B, 0xF3, 0xA9, 0x48, 0x4C, 0x51, 0x42, 0xE6, 0x8A,
	0xCB, 0xB9, 0x62, 0x28, 0x02, 0xBB, 0x06, 0xBD, 0x2A, 0x9B, 0xB6, 0x42,
	0x51, 0x6B, 0x3F, 0x45, 0x09, 0xB9, 0x55, 0x48, 0xBA, 0x24, 0xC4, 0xB7,
	0x8B, 0xB2, 0xBA, 0xAF, 0x7A, 0x53, 0x1B, 0xB7, 0xE5, 0x33, 0x6D, 0xBA,
	0x3B, 0xAA, 0xBA, 0x3B, 0x2A, 0x95, 0x90, 0x7B, 0x3A, 0x4B, 0xF9, 0x5D,
	0x27, 0x84, 0x82, 0x80, 0x9A, 0xF3, 0x6E, 0x05, 0xD5, 0x76, 0xC3, 0x8C,
	0xEA, 0x0F, 0x54, 0xD3, 0x87, 0xAB, 0x2B, 0x6E, 0x32, 0x0B, 0x6E, 0xB1,
	0x22, 0x9A, 0x29, 0x70, 0x3C, 0xC5, 0x5E, 0x86, 0x94, 0x2B, 0x99, 0x96,
	0x21, 0xA7, 0x64, 0xA8, 0xBB, 0xB2, 0x44, 0xAE, 0x0C, 0x04, 0x07, 0x73,
	0x83, 0x99, 0xC5, 0x6E, 0x53, 0x41, 0x65, 0x6A, 0x10, 0x5F, 0x05, 0x97,
	0xBE, 0x42, 0x60, 0xDE, 0x44, 0xA6, 0x8A, 0xD3, 0x03, 0x27, 0x03, 0x70,
	0xEA, 0x4C, 0x05, 0x60, 0xA7, 0x02, 0x8B, 0xA6, 0x82, 0xF2, 0x5F, 0x87,
	0xA5, 0xF2, 0x3B, 0x4A, 0x95, 0x94, 0x28, 0xC1, 0x09, 0x3A, 0xD0, 0x7D,
	0x1D, 0x99, 0x03, 0x5D, 0x87, 0xE9, 0x0D, 0xDB, 0xF9, 0xED, 0xA5, 0x0A,
	0x3E, 0x11, 0xB5, 0x97, 0x28, 0x99, 0x89, 0x18, 0x0D, 0xDB, 0x05, 0x6D,
	0xA5, 0x4A, 0x4A, 0x94, 0xE0, 0x7A, 0xDB, 0xD0, 0xFD, 0xED, 0xE0, 0x40,
	0x67, 0x10, 0x83, 0xE5, 0xDA, 0xC7, 0x2B, 0xFD, 0x48, 0x49, 0x3F, 0x0F,
	0xD7, 0xCF, 0xC3, 0x88, 0x5A, 0xB3, 0xAE, 0xB7, 0x05, 0x43, 0xD6, 0xD7,
	0x58, 0xA5, 0x6A, 0xE0, 0xAF, 0xB3, 0x0A, 0x07, 0x1B, 0x8E, 0xDF, 0x6C,
	0x0A, 0xAB, 0xAF, 0xDD, 0x75, 0xFF, 0x2C, 0x41, 0x8D, 0xD2, 0xFB, 0x67,
	0xB1, 0xA3, 0xA4, 0xD3, 0xE3, 0x94, 0x58, 0x1E, 0xC9, 0x63, 0x3A, 0xB9,
	0x56, 0x0D, 0xE6, 0x74, 0x4A, 0xF5, 0x74, 0x2A, 0x65, 0x1A, 0x04, 0x6F,
	0x9C, 0x0A, 0x7D, 0x1D, 0x8E, 0x57, 0x03, 0xA7, 0x20, 0xA2, 0xF8, 0x4D,
	0xE4, 0x99, 0xB7, 0x31, 0x69, 0xFD, 0x5C, 0x9C, 0x1A, 0x58, 0x21, 0xB7,
	0x58, 0xC8, 0x2D, 0xB8, 0xC2, 0x03, 0x07, 0x5B, 0x11, 0xFF, 0x5F, 0x24,
	0xE4, 0xE2, 0xD4, 0x50, 0x44, 0x22, 0x28, 0xE2, 0x82, 0x83, 0x3C, 0x84,
	0x90, 0x83, 0x53, 0x03, 0x2B, 0xE2, 0x14, 0x8B, 0x38, 0x05, 0x62, 0x0E,
	0x28, 0xE1, 0x85, 0x88, 0x9B, 0x70, 0x6A, 0x60, 0xC5, 0xEC, 0xE2, 0x01,
	0x76, 0xC1, 0x35, 0x76, 0xD6, 0x8D, 0x96, 0xD0, 0xA1, 0x3A, 0xDC, 0x6C,
	0x8A, 0x6F, 0xD4, 0xA1, 0x87, 0xEB, 0x8F, 0xDF, 0xBE, 0x10, 0x39, 0x46,
	0xC0, 0xAB, 0x81, 0x1B, 0x23, 0x60, 0xC7, 0x88, 0x85, 0xE3, 0x65, 0xB9,
	0x8F, 0x49, 0xC8, 0xF7, 0xE9, 0x25, 0x6A, 0xE0, 0x95, 0x60, 0xDF, 0x67,
	0xA0, 0xE5, 0xD0, 0x77, 0xC8, 0xE7, 0x4F, 0xD1, 0xCF, 0xFE, 0x7F, 0x66,
	0xFF, 0x68, 0x17, 0x67, 0xE5, 0x7A, 0x56, 0x53, 0x53, 0xB5, 0xA8, 0xFD,
	0xC5, 0x6A, 0x15, 0x88, 0x0D, 0x42, 0x06, 0xA9, 0xAF, 0x5D, 0x7F, 0xEF,
	0xF8, 0x3F, 0x0B, 0x10, 0x3B, 0xD9
};
};

bool
sk_png_remove_chunk (const char* szName, void* data, size_t& size)
{
  if (szName == nullptr || data == nullptr || size < 12 || strlen (szName) < 4)
  {
    return false;
  }

  size_t   erase_pos = 0;
  uint8_t* erase_ptr = nullptr;

  // Effectively a string search, but ignoring nul-bytes in both
  //   the character array being searched and the pattern...
  std::string_view data_view ((const char *)data, size);
  if (erase_pos  = data_view.find (szName, 0, 4);
      erase_pos == data_view.npos)
  {
    return false;
  }

  erase_pos -= 4; // Rollback to the chunk's length field
  erase_ptr =
    ((uint8_t *)data + erase_pos);

  uint32_t chunk_size = *(uint32_t *)erase_ptr;

// Length is Big Endian, Intel/AMD CPUs are Little Endian
#if (defined _M_IX86) || (defined _M_X64)
  chunk_size = _byteswap_ulong (chunk_size);
#endif

  size_t size_to_erase = (size_t)12 + chunk_size;

  memmove ( erase_ptr,
            erase_ptr             + size_to_erase,
                 size - erase_pos - size_to_erase );

  size -= size_to_erase;

  return true;
}

SK_PNG_HDR_cLLi_Payload
SK_HDR_CalculateContentLightInfo (const DirectX::Image& img)
{
  SK_PNG_HDR_cLLi_Payload clli;

  static const XMMATRIX c_from709to2020 =
  {
    0.627225305694944f,  0.0690418812810714f, 0.0163911702607078f, 0.0f,
    0.329476882715808f,  0.919605681354755f,  0.0880887513437058f, 0.0f,
    0.0432978115892484f, 0.0113524373641739f, 0.895520078395586f,  0.0f,
    0.0f,                0.0f,                0.0f,                1.0f
  };

  static const XMMATRIX c_from709toXYZ =
  {
    0.4123907983303070068359375f,  0.2126390039920806884765625f,   0.0193308182060718536376953125f, 0.0f,
    0.3575843274593353271484375f,  0.715168654918670654296875f,    0.119194783270359039306640625f,  0.0f,
    0.18048079311847686767578125f, 0.072192318737506866455078125f, 0.950532138347625732421875f,     0.0f,
    0.0f,                          0.0f,                           0.0f,                            1.0f
  };

  static const XMMATRIX c_from2020toXYZ =
  {
    0.636958062f, 0.2627002000f, 0.0000000000f, 0.0f,
    0.144616901f, 0.6779980650f, 0.0280726924f, 0.0f,
    0.168880969f, 0.0593017153f, 1.0609850800f, 0.0f,
    0.0f,         0.0f,          0.0f,          1.0f
  };
  
  struct ParamsPQ
  {
    XMVECTOR N, M;
    XMVECTOR C1, C2, C3;
    XMVECTOR MaxPQ;
  };
  
  static const ParamsPQ PQ =
  {
    XMVectorReplicate (2610.0 / 4096.0 / 4.0),   // N
    XMVectorReplicate (2523.0 / 4096.0 * 128.0), // M
    XMVectorReplicate (3424.0 / 4096.0),         // C1
    XMVectorReplicate (2413.0 / 4096.0 * 32.0),  // C2
    XMVectorReplicate (2392.0 / 4096.0 * 32.0),  // C3
    XMVectorReplicate (125.0),
  };

  auto PQToLinear = [](XMVECTOR N)
  {
    XMVECTOR ret;

    XMVECTOR sign =
      XMVectorSet (XMVectorGetX (N) < 0.0f ? -1.0f : 1.0f,
                   XMVectorGetY (N) < 0.0f ? -1.0f : 1.0f,
                   XMVectorGetZ (N) < 0.0f ? -1.0f : 1.0f, 1.0f);

    ret =
      XMVectorPow (XMVectorAbs (N), XMVectorDivide (g_XMOne, PQ.M));

    XMVECTOR nd;

    nd =
      XMVectorDivide (
        XMVectorMax (XMVectorSubtract (ret, PQ.C1), g_XMZero),
                     XMVectorSubtract (     PQ.C2,
              XMVectorMultiply (PQ.C3, ret)));

    ret =
      XMVectorMultiply (XMVectorMultiply (XMVectorPow (nd, XMVectorDivide (g_XMOne, PQ.N)), PQ.MaxPQ), sign);

    return ret;
  };

  float N         =       0.0f;
  float fLumAccum =       0.0f;
  float fMaxLum   =       0.0f;
  float fMinLum   = 5240320.0f;

  EvaluateImage ( img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      float fScanlineLum = 0.0f;

      switch (img.format)
      {
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR v =
              *pixels++;

            v =
              XMVector3Transform (
                PQToLinear (XMVectorSaturate (v)), c_from2020toXYZ
              );

            const float fLum =
              XMVectorGetY (v);

            fMaxLum =
              std::max (fMaxLum, fLum);

            fMinLum =
              std::min (fMinLum, fLum);

            fScanlineLum += fLum;
          }
        } break;

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        {
          for (size_t j = 0; j < width; ++j)
          {
            XMVECTOR v =
              *pixels++;

            v =
              XMVector3Transform (v, c_from709toXYZ);

            const float fLum =
              XMVectorGetY (v);

            fMaxLum =
              std::max (fMaxLum, fLum);

            fMinLum =
              std::min (fMinLum, fLum);

            fScanlineLum += fLum;
          }
        } break;

        default:
          break;
      }

      fLumAccum +=
        (fScanlineLum / static_cast <float> (width));
      ++N;
    }
  );

  if (N > 0.0)
  {
    // 0 nits - 10k nits (appropriate for screencap, but not HDR photography)
    fMinLum = std::clamp (fMinLum, 0.0f,    125.0f);
    fMaxLum = std::clamp (fMaxLum, fMinLum, 125.0f);

    const float fLumRange =
            (fMaxLum - fMinLum);

    auto        luminance_freq = std::make_unique <uint32_t []> (65536);
    ZeroMemory (luminance_freq.get (),     sizeof (uint32_t)  *  65536);

    EvaluateImage ( img,
    [&](const XMVECTOR* pixels, size_t width, size_t y)
    {
      UNREFERENCED_PARAMETER(y);

      for (size_t j = 0; j < width; ++j)
      {
        XMVECTOR v = *pixels++;

        v =
          XMVector3Transform (v, c_from709toXYZ);

        luminance_freq [
          std::clamp ( (int)
            std::roundf (
              (XMVectorGetY (v) - fMinLum)     /
                                    (fLumRange / 65536.0f) ),
                                              0, 65535 ) ]++;
      }
    });

          double percent  = 100.0;
    const double img_size = (double)img.width *
                            (double)img.height;

    for (auto i = 65535; i >= 0; --i)
    {
      percent -=
        100.0 * ((double)luminance_freq [i] / img_size);

      if (percent <= 99.9)
      {
        fMaxLum =
          fMinLum + (fLumRange * ((float)i / 65536.0f));

        break;
      }
    }

    SK_PNG_SetUint32 (clli.max_cll,
      static_cast <uint32_t> ((80.0f *          fMaxLum) / 0.0001f));
    SK_PNG_SetUint32 (clli.max_fall,
      static_cast <uint32_t> ((80.0f * (fLumAccum / N))  / 0.0001f));
  }

  return clli;
}

bool
SK_PNG_MakeHDR ( const wchar_t*        wszFilePath,
                 const DirectX::Image& encoded_img,
                 const DirectX::Image& raw_img )
{
  std::ignore = encoded_img;

  static const BYTE _test [] = { 0x49, 0x45, 0x4E, 0x44 };

  if (png_crc32 ((const BYTE *)_test, 0, 4, 0) == 0xae426082)
  {
    FILE*
        fPNG = _wfopen (wszFilePath, L"r+b");
    if (fPNG != nullptr)
    {
      fseek  (fPNG, 0, SEEK_END);
      size_t size = ftell (fPNG);
      rewind (fPNG);

      auto data =
        std::make_unique <uint8_t []> (size);

      if (! data)
      {
        fclose (fPNG);
        return false;
      }

      fread (data.get (), size, 1, fPNG);
      rewind (                     fPNG);

      sk_png_remove_chunk ("sRGB", data.get (), size);
      sk_png_remove_chunk ("gAMA", data.get (), size);

      fwrite (data.get (), size, 1, fPNG);

      // Truncate the file
      _chsize (_fileno (fPNG), static_cast <long> (size));

      size_t         insert_pos = 0;
      const uint8_t* insert_ptr = nullptr;

      // Effectively a string search, but ignoring nul-bytes in both
      //   the character array being searched and the pattern...
      std::string_view  data_view ((const char *)data.get (), size);
      if (insert_pos  = data_view.find ("IDAT", 0, 4);
          insert_pos == data_view.npos)
      {
        fclose (fPNG);
        return false;
      }

      insert_pos -= 4; // Rollback to the chunk's length field
      insert_ptr =
        (data.get () + insert_pos);

      fseek (fPNG, static_cast <long> (insert_pos), SEEK_SET);

      struct SK_PNG_Chunk {
        uint32_t      len;
        unsigned char name [4];
        void*         data;
        uint32_t      crc;
        uint32_t      _native_len;

        void write (FILE* fStream)
        {
          // Length is Big Endian, Intel/AMD CPUs are Little Endian
          if (_native_len == 0)
          {
            _native_len = len;
#if (defined _M_IX86) || (defined _M_X64)
            len         = _byteswap_ulong (_native_len);
#endif
          }

          crc =
            png_crc32 (data, 0, _native_len, png_crc32 (name, 0, 4, 0x0));

#if (defined _M_IX86) || (defined _M_X64)
          crc = _byteswap_ulong (crc);
#endif

          fwrite (&len, 8,           1, fStream);
          fwrite (data, _native_len, 1, fStream);
          fwrite (&crc, 4,           1, fStream);
        };
      };

      uint8_t cicp_data [] = {
        9,  // BT.2020 Color Primaries
        16, // ST.2084 EOTF (PQ)
        0,  // Identity Coefficients
        1,  // Full Range
      };

      // Embedded ICC Profile so that Discord will render in HDR
      SK_PNG_HDR_iCCP_Payload iccp_data;

      SK_PNG_HDR_cHRM_Payload chrm_data; // Rec 2020 chromaticity
      SK_PNG_HDR_sBIT_Payload sbit_data; // Bits in original source (max=12)
      SK_PNG_HDR_mDCv_Payload mdcv_data; // Display capabilities
      SK_PNG_HDR_cLLi_Payload clli_data; // Content light info

      clli_data =
        SK_HDR_CalculateContentLightInfo (raw_img);

      sbit_data = {
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format)),
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format)),
        static_cast <unsigned char> (DirectX::BitsPerColor (raw_img.format))
      };

      // If using compression optimization, max bits = 12
      sbit_data.red_bits   = 16;//std::min (sbit_data.red_bits,   12ui8);
      sbit_data.green_bits = 16;//std::min (sbit_data.green_bits, 12ui8);
      sbit_data.blue_bits  = 16;//std::min (sbit_data.blue_bits,  12ui8);

      auto& rb =
        SK_GetCurrentRenderBackend ();

      auto& active_display =
        rb.displays [rb.active_display];

      SK_PNG_SetUint32 (mdcv_data.luminance.minimum,
        static_cast <uint32_t> (round (active_display.gamut.minY / 0.0001f)));
      SK_PNG_SetUint32 (mdcv_data.luminance.maximum,
        static_cast <uint32_t> (round (active_display.gamut.maxY / 0.0001f)));

      SK_PNG_SetUint32 (mdcv_data.primaries.red_x,
        static_cast <uint32_t> (round (active_display.gamut.xr / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.red_y,
        static_cast <uint32_t> (round (active_display.gamut.yr / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.primaries.green_x,
        static_cast <uint32_t> (round (active_display.gamut.xg / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.green_y,
        static_cast <uint32_t> (round (active_display.gamut.yg / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.primaries.blue_x,
        static_cast <uint32_t> (round (active_display.gamut.xb / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.primaries.blue_y,
        static_cast <uint32_t> (round (active_display.gamut.yb / 0.00002)));

      SK_PNG_SetUint32 (mdcv_data.white_point.x,
        static_cast <uint32_t> (round (active_display.gamut.Xw / 0.00002)));
      SK_PNG_SetUint32 (mdcv_data.white_point.y,
        static_cast <uint32_t> (round (active_display.gamut.Yw / 0.00002)));

      SK_PNG_Chunk iccp_chunk = { sizeof (SK_PNG_HDR_iCCP_Payload), { 'i','C','C','P' }, &iccp_data };
      SK_PNG_Chunk cicp_chunk = { sizeof (cicp_data),               { 'c','I','C','P' }, &cicp_data };
      SK_PNG_Chunk clli_chunk = { sizeof (clli_data),               { 'c','L','L','i' }, &clli_data };
      SK_PNG_Chunk sbit_chunk = { sizeof (sbit_data),               { 's','B','I','T' }, &sbit_data };
      SK_PNG_Chunk chrm_chunk = { sizeof (chrm_data),               { 'c','H','R','M' }, &chrm_data };
      SK_PNG_Chunk mdcv_chunk = { sizeof (mdcv_data),               { 'm','D','C','v' }, &mdcv_data };

      iccp_chunk.write (fPNG);
      cicp_chunk.write (fPNG);
      clli_chunk.write (fPNG);
      sbit_chunk.write (fPNG);
      chrm_chunk.write (fPNG);
      mdcv_chunk.write (fPNG);

      // Write the remainder of the original file
      fwrite (insert_ptr, size - insert_pos, 1, fPNG);

      auto final_size =
        ftell (fPNG);

      auto full_png =
        std::make_unique <unsigned char []> (final_size);

      rewind (fPNG);
      fread  (full_png.get (), final_size, 1, fPNG);
      fclose (fPNG);

      SK_LOGi1 (L"Applied HDR10 PNG chunks to %ws.", wszFilePath);
      SK_LOGi1 (L" >> MaxCLL: %.6f nits, MaxFALL: %.6f nits",
                static_cast <double> (SK_PNG_GetUint32 (clli_data.max_cll))  * 0.0001,
                static_cast <double> (SK_PNG_GetUint32 (clli_data.max_fall)) * 0.0001);
      SK_LOGi1 (L" >> Mastering Display Min/Max Luminance: %.6f/%.6f nits",
                static_cast <double> (SK_PNG_GetUint32 (mdcv_data.luminance.minimum)) * 0.0001,
                static_cast <double> (SK_PNG_GetUint32 (mdcv_data.luminance.maximum)) * 0.0001);

      return true;
    }
  }

  return false;
}



SK_ScreenshotManager::SnippingState
SK_ScreenshotManager::getSnipState (void) const
{
  return snip_state;
}

void
SK_ScreenshotManager::setSnipState (SK_ScreenshotManager::SnippingState state)
{
  static thread_local
    int _ExtraCursorRefs = 0;

  if (snip_state == state)
    return;

  if (state == SnippingInactive ||
      state == SnippingComplete)
  {
    if (     _ExtraCursorRefs > 0) {
      while (_ExtraCursorRefs-- > 0)
      {
        ShowCursor (FALSE);
      }
    }
  }

  else if (state == SnippingRequested ||
           state == SnippingActive)
  {
    if (! SK_InputUtil_IsHWCursorVisible ())
    {
      do
      {
        ++_ExtraCursorRefs;
      } while (ShowCursor (TRUE) < 0);
    }
  }

  snip_state = state;
}

DirectX::Rect
SK_ScreenshotManager::getSnipRect (void) const
{
  return snip_rect;
}

void
SK_ScreenshotManager::setSnipRect (const DirectX::Rect& rect)
{
  snip_rect = rect;
}