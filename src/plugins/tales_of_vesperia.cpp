﻿//
// Copyright 2019 No "Redacted" Name
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#include <SpecialK/stdafx.h>
#include <SpecialK/render/d3d11/d3d11_core.h>


#define TVFIX_VERSION_NUM L"0.5.2.5"
#define TVFIX_VERSION_STR LR"(Tales of Vesperia "Fix" v )" TVFIX_VERSION_NUM

#pragma warning(push)
#pragma warning(disable: 4244)

bool                      SK_TVFix_PlugInCfg         (void);
HRESULT STDMETHODCALLTYPE SK_TVFix_PresentFirstFrame (IUnknown* pSwapChain, UINT SyncInterval, UINT Flags);
void                      SK_TVFix_BeginFrame        (void);

extern volatile LONG SK_D3D11_DrawTrackingReqs;

struct tv_mem_addr_s
{
  const char*    pattern        = nullptr;
  const char*    pattern_mask   = nullptr;
  size_t         pattern_len    =       0;

  size_t         rep_size       =       0;
  ptrdiff_t      rep_off        =       0;

  bool           enabled        =   false;
  void*          scanned_addr   = nullptr;

  std::vector <BYTE> orig_bytes;

  const wchar_t* desc           = nullptr;
  void*          expected_addr  = nullptr;

  void scan (void)
  {
    if (scanned_addr == nullptr)
    {
      // First try the expected addressexpected_addr
      if (expected_addr != nullptr)
      {
        void* expected =
          (void *)((uintptr_t)SK_GetModuleHandle (nullptr) + (uintptr_t)expected_addr);

        auto orig_se =
        SK_SEH_ApplyTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
        try
        {
          if (0 == memcmp (pattern, expected, pattern_len))
            scanned_addr = expected;
        }

        //__except ( ( GetExceptionCode () == EXCEPTION_ACCESS_VIOLATION ) ?
        //         EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH )
        catch (...)
        {
        }
        SK_SEH_RemoveTranslator (orig_se);

        if (scanned_addr == nullptr)
        {
          scanned_addr =
            SK_ScanAlignedEx (pattern, pattern_len, pattern_mask, (void *)((uintptr_t)SK_GetModuleHandle (nullptr) + (uintptr_t)expected_addr));
        }
      }

      // Fallback to exhaustive search if not there
      if (scanned_addr == nullptr)
      {
        auto orig_se =
        SK_SEH_ApplyTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
        try {
          scanned_addr =
            SK_ScanAlignedEx (pattern, pattern_len, pattern_mask);
        }

        //__except ( ( GetExceptionCode () == EXCEPTION_ACCESS_VIOLATION ) ?
        //         EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH )
        catch (...)
        {
        }
        SK_SEH_RemoveTranslator (orig_se);
      }

      dll_log->Log (L"Scanned address for: %s: %p (alignment=%lu)", desc, scanned_addr, (uintptr_t)scanned_addr % 16);

      if (scanned_addr != nullptr)
      {
        orig_bytes.clear ();

        for ( UINT i = 0; i < rep_size; i++ )
        {
          orig_bytes.push_back (*(uint8_t *)((uintptr_t)scanned_addr + rep_off + i));
        }
      }
    }
  }

  bool toggle (void)
  {
    if (enabled)
      disable ();
    else
      enable ();

    return
      enabled;
  }

  void enable (void)
  {
    scan ();

    if (scanned_addr != nullptr && (! enabled))
    {
      DWORD dwProtect;

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, PAGE_EXECUTE_READWRITE, &dwProtect);

      if (rep_size == 8 && (((intptr_t)scanned_addr % 8) == 0))
      {
        InterlockedExchange64 ((volatile LONG64*)((intptr_t)scanned_addr + rep_off), *(__int64 *)orig_bytes.data ());
      }

      else if (rep_size == 2 && (((intptr_t)scanned_addr % 2) == 0))
      {
        InterlockedExchange16 ((volatile SHORT*)((intptr_t)scanned_addr + rep_off), *(SHORT *)"\x77\xBD");
      }

      // Other sizes are not atomic, so... not threadsafe
      else
      {
        memcpy ((void *)((intptr_t)scanned_addr + rep_off), orig_bytes.data (), rep_size);
      }

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, dwProtect, &dwProtect);

      enabled = true;
    }
  }

  void disable (void)
  {
    scan ();

    if (scanned_addr != nullptr && enabled)
    {
      DWORD dwProtect;

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, PAGE_EXECUTE_READWRITE, &dwProtect);

      if (rep_size == 8 && (((intptr_t)scanned_addr % 8) == 0))
      {
        InterlockedExchange64 ((volatile LONG64*)((intptr_t)scanned_addr + rep_off), *(LONG64 *)"\x90\x90\x90\x90\x90\x90\x90\x90");
      }

      else if (rep_size == 2 && (((intptr_t)scanned_addr % 2) == 0))
      {
        InterlockedExchange16 ((volatile SHORT*)((intptr_t)scanned_addr + rep_off), *(SHORT *)"\x90\x90");
      }

      else
      {
        memset ((void *)((intptr_t)scanned_addr + rep_off), 0x90, rep_size);
      }

      VirtualProtect ((void*)((intptr_t)scanned_addr + rep_off), rep_size, dwProtect, &dwProtect);

      enabled = false;
    }
  }
};


struct SK_TVFix_ModContext {
  bool  __SK_TVFix_NoRenderSleep        = true;

  //
  // Callbacks are running on the wrong thread
  //
  //  ==> Serious deadlock hazard! <==
  //
  //  * We'll run the callbacks manually
  //      from the swapchain thread.
  //
  DWORD __SK_TVFix_MagicSteamThread     = 0;

  tv_mem_addr_s instn__model_animation  = { };
  tv_mem_addr_s instn__particle_effects = { };
  tv_mem_addr_s instn__blur             = { };
  tv_mem_addr_s instn__depth_of_field   = { };
  tv_mem_addr_s instn__bloom            = { };
  tv_mem_addr_s instn__draw_HUD         = { };

  // TOV_DE.exe+72AE99 - E8 92C0FCFF           - call TOV_DE.exe+6F6F30 { Depth of Field }
  // TOV_DE.exe+6F632A - E8 C15C0300           - call TOV_DE.exe+72BFF0 { Bloom Lighting }
  // TOV_DE.exe+6F6375 - E8 A69E0300           - call TOV_DE.exe+730220 { Bloom Lighting 2 }


  bool __SK_TVFix_AspectRatioCorrection = false;

  sk::ParameterBool* _SK_TVFix_DisableDepthOfField;
  bool              __SK_TVFix_DisableDepthOfField = false;

  sk::ParameterBool* _SK_TVFix_DisableBlur;
  bool              __SK_TVFix_DisableBlur = false;

  sk::ParameterBool* _SK_TVFix_DisableBloom;
  bool              __SK_TVFix_DisableBloom = false;

  sk::ParameterBool* _SK_TVFix_ActiveAntiStutter;
  bool              __SK_TVFix_ActiveAntiStutter = true;

  sk::ParameterBool* _SK_TVFix_SharpenShadows;
  bool              __SK_TVFix_SharpenShadows = true;

  sk::ParameterBool* _SK_TVFix_FixMSAA;
  bool              __SK_TVFix_FixMSAA = true;

  sk::ParameterInt* _SK_TVFix_MultisampleCount;
  int              __SK_TVFix_MultisampleCount = 4;

  sk::ParameterInt* _SK_TVFix_LastKnown_XRes;
  int              __SK_TVFix_LastKnown_XRes = 3840;

  sk::ParameterInt* _SK_TVFix_LastKnown_YRes;
  int              __SK_TVFix_LastKnown_YRes = 2160;

  volatile LONG    __TVFIX_init = 0;
};
SK_LazyGlobal <SK_TVFix_ModContext> tvfix_ctx;

#define PS_CRC32_SHADOWFILTER 0x84da24a5

extern bool
__stdcall
SK_FetchVersionInfo (const wchar_t* wszProduct);

extern HRESULT
__stdcall
SK_UpdateSoftware (const wchar_t* wszProduct);

unsigned int
__stdcall
SK_TVFix_CheckVersion (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  SK_Thread_Create ([](LPVOID)->
    DWORD
    {
      while (SK_GetFramesDrawn () < 5)
        ;

      // 12/28/20: Disabled version checks, since I don't intend to ever update this thing again.
      //
      ////if (SK_FetchVersionInfo (L"TVF"))
      ////    SK_UpdateSoftware   (L"TVF");

      SK_Thread_CloseSelf ();

      return 0;
    }
  );

  return 0;
}


HRESULT
STDMETHODCALLTYPE
SK_TVFix_PresentFirstFrame (IUnknown* pSwapChain, UINT SyncInterval, UINT Flags)
{
  UNREFERENCED_PARAMETER (pSwapChain);
  UNREFERENCED_PARAMETER (SyncInterval);
  UNREFERENCED_PARAMETER (Flags);

  auto& plugin_ctx =
    tvfix_ctx.get ();

  if (! InterlockedCompareExchange (&plugin_ctx.__TVFIX_init, 1, 0))
  {
    plugin_ctx.__SK_TVFix_MagicSteamThread = SK_Thread_GetCurrentId ();

    SK_TVFix_CheckVersion (nullptr);
  }

  return S_OK;
}



extern "C" void __cdecl
SteamAPI_RunCallbacks_Detour (void);

typedef void (__cdecl *SteamAPI_RunCallbacks_Hook_pfn)(void);
                static SteamAPI_RunCallbacks_Hook_pfn
                       SteamAPI_RunCallbacks_Local = nullptr;

static void
SteamAPI_RunCallbacks_Preamble (void)
{
  if (SK_Thread_GetCurrentId () != tvfix_ctx->__SK_TVFix_MagicSteamThread)
    return;

  SteamAPI_RunCallbacks_Local ();
}


void
SK_TVFix_InitPlugin (void)
{
  auto& plugin_ctx =
         tvfix_ctx.get ();

  SK_SetPluginName (TVFIX_VERSION_STR);

  plugin_mgr->config_fns.emplace      (SK_TVFix_PlugInCfg);
  plugin_mgr->first_frame_fns.emplace (SK_TVFix_PresentFirstFrame);
  plugin_mgr->begin_frame_fns.emplace (SK_TVFix_BeginFrame);

  SK_CreateFuncHook (      L"SteamAPI_RunCallbacks_Detour",
                             SteamAPI_RunCallbacks_Detour,
                             SteamAPI_RunCallbacks_Preamble,
    static_cast_p2p <void> (&SteamAPI_RunCallbacks_Local) );
  SK_EnableHook     (        SteamAPI_RunCallbacks_Detour );


  plugin_ctx._SK_TVFix_LastKnown_XRes =
    _CreateConfigParameterInt ( L"TVFix.Render",
                               L"LastRenderWidth", plugin_ctx.__SK_TVFix_LastKnown_XRes,
                               L"Store the last known width" );

  plugin_ctx._SK_TVFix_LastKnown_YRes =
    _CreateConfigParameterInt ( L"TVFix.Render",
                               L"LastRenderHeight", plugin_ctx.__SK_TVFix_LastKnown_YRes,
                               L"Store the last known height" );


  plugin_ctx.instn__model_animation =
  { "\xE8\xCE\x1C\x00\x00",
    //----------------------------------------------//
    "\xFF\xFF\xFF\xFF\xFF",
    5, 5, 0,    true, nullptr,

    { }, L"Enable Model Animation", (void *)0x6F7EED };

  plugin_ctx.instn__particle_effects =
  { "\xE8\xA6\xD3\xFF\xFF",
    //----------------------------------------------//
    "\xFF\xFF\xFF\xFF\xFF",
    5, 5, 0,    true, nullptr,

    { }, L"Enable Particle Effects", (void *) 0x6F7EF5 };

  //"\xE8\xC6\xE2\xFF\xFF", -- All Post-Processing

  plugin_ctx.instn__blur =
  {
    "\xE8\x92\xC0\xFC\xFF",

    //----------------------------------------------//
    "\xFF\xFF\xFF\xFF\xFF",
    5, 5, 0,    true, nullptr,

    { }, L"Enable Blur", (void *)0x72AE99 };

  plugin_ctx.instn__depth_of_field =
  {
    "\xE8\xA6\x9E\x03\x00",
    //----------------------------------------------//
    "\xFF\xFF\xFF\xFF\xFF",
    5, 5, 0,    true, nullptr,

    { }, L"Enable Depth of Field", (void *)0x6F6375 };

  plugin_ctx.instn__bloom =
  {
    "\xE8\xC1\x5C\x03\x00",

    //----------------------------------------------//
    "\xFF\xFF\xFF\xFF\xFF",
    5, 5, 0,    true, nullptr,

    { }, L"Enable Bloom Lighting", (void *)0x6f632a };
}

bool
SK_TVFix_PlugInCfg (void)
{
  auto& plugin_ctx =
    tvfix_ctx.get ();

  if (ImGui::CollapsingHeader ("Tales of Vesperia Definitive Edition", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TreePush ("");

    if (ImGui::Checkbox ("Aggressive Anti-Stutter", &plugin_ctx.__SK_TVFix_ActiveAntiStutter))
    {
      plugin_ctx._SK_TVFix_ActiveAntiStutter->store (plugin_ctx.__SK_TVFix_ActiveAntiStutter);
    }

    if (ImGui::IsItemHovered ())
        ImGui::SetTooltip ("Eliminate Microstutter, but will raise CPU usage %%");

    ImGui::SameLine (); ImGui::Spacing (); ImGui::SameLine (); ImGui::Spacing ();
    ImGui::SameLine (); ImGui::Spacing (); ImGui::SameLine ();

    ImGui::BeginGroup ();
    ImGui::Checkbox   ("Fix MSAA###SK_TVFIX_MSAA", &plugin_ctx.__SK_TVFix_FixMSAA);

    if (ImGui::IsItemHovered ())
    {
      ImGui::SetTooltip ("Render the Entire Scene Using MSAA Instead of Only a Handful of Geometry.");
    }

    if (plugin_ctx.__SK_TVFix_FixMSAA || config.render.dxgi.msaa_samples != SK_NoPreference)
    {
      ImGui::SameLine ();

      int sample_idx;

      switch (config.render.dxgi.msaa_samples)
      {
        default:
        case -1:
          sample_idx = 0;
          break;

        case 2:
          sample_idx = 1;
          break;

        case 4:
          sample_idx = 2;
          break;

        case 8:
          sample_idx = 3;
          break;
      }

      static int orig_samples =
        config.render.dxgi.msaa_samples;

      if (ImGui::Combo ("###SK_TVFix_MSAA_Combo", &sample_idx, "No Override\0 2x\0 4x\0 8x\0\0"))
      {
        if (sample_idx > 0)
        {
          config.render.dxgi.msaa_samples =
            ( 1 << sample_idx );
        }

        else
        {
          config.render.dxgi.msaa_samples = SK_NoPreference;
        }
      }

      if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("NOTE: You must set the game's gfx settings to 4x MSAA for these overrides to work.");

      if (orig_samples != config.render.dxgi.msaa_samples)
      {
        ImGui::PushStyleColor (ImGuiCol_Text, (ImVec4&&)ImColor::HSV (.3f, .8f, .9f));
        ImGui::BulletText     ("Game Restart Required");
        ImGui::PopStyleColor  ();
      }
    }
    ImGui::EndGroup ();
#if 0
    ImGui::SameLine        (             );
    ImGui::TextUnformatted ("Gamepad:   ");
    ImGui::SameLine        (             );

    static int buttons = 0;

    ImGui::RadioButton ("Xbox 360##TVFix_XBox360Icons",  &buttons, 0); ImGui::SameLine ();
    ImGui::RadioButton ("PlayStation 3##TVFix_PS3Icons", &buttons, 1); ImGui::SameLine ();
    ImGui::RadioButton ("PlayStation 4##TVFix_PS4Icons", &buttons, 2);
#endif

    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.02f, 0.68f, 0.90f, 0.45f));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.07f, 0.72f, 0.90f, 0.80f));
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImVec4 (0.14f, 0.78f, 0.87f, 0.80f));

    ImGui::BeginGroup ();
    if (ImGui::CollapsingHeader ("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen |
                                                    ImGuiTreeNodeFlags_AllowOverlap))
    {
      ImGui::TreePush ("");

      bool enable = (! plugin_ctx.__SK_TVFix_DisableDepthOfField);

      if ( ImGui::Checkbox ("Enable Depth of Field", &enable) )
      {
        //instn__depth_of_field.enabled = !instn__depth_of_field.enabled;
        //instn__depth_of_field.toggle ();
        //_SK_TVFix_DisableDepthOfField->store (! instn__depth_of_field.enabled);

        if (enable)
        {
          SK_D3D11_Shaders->pixel.releaseTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x27fbcdeb);
          SK_D3D11_Shaders->pixel.releaseTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x8dfd78fd);
          InterlockedDecrement (&SK_D3D11_DrawTrackingReqs);
        }

        else
        {
          SK_D3D11_Shaders->pixel.addTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x27fbcdeb);
          SK_D3D11_Shaders->pixel.addTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x8dfd78fd);
          InterlockedIncrement (&SK_D3D11_DrawTrackingReqs);
        }

        plugin_ctx.__SK_TVFix_DisableDepthOfField = (! enable);

        plugin_ctx._SK_TVFix_DisableDepthOfField->store (plugin_ctx.__SK_TVFix_DisableDepthOfField);
      }

      if ( plugin_ctx.instn__bloom.scanned_addr != nullptr &&
           ImGui::Checkbox ("Enable Atmospheric Bloom", &plugin_ctx.instn__bloom.enabled) )
      {
        plugin_ctx.instn__bloom.enabled = (! plugin_ctx.instn__bloom.enabled);
        plugin_ctx.instn__bloom.toggle ();

        plugin_ctx._SK_TVFix_DisableBloom->store (! plugin_ctx.instn__bloom.enabled);
      }

      if ( plugin_ctx.instn__blur.scanned_addr != nullptr &&
           ImGui::Checkbox ("Enable Fullscene Blur", &plugin_ctx.instn__blur.enabled) )
      {
        plugin_ctx.instn__blur.enabled = (! plugin_ctx.instn__blur.enabled);
        plugin_ctx.instn__blur.toggle ();

        plugin_ctx._SK_TVFix_DisableBlur->store (! plugin_ctx.instn__blur.enabled);
      }

      if (ImGui::Checkbox ("Sharpen Shadows", &plugin_ctx.__SK_TVFix_SharpenShadows))
      {
        //if (__SK_TVFix_SharpenShadows)
        //  SK_D3D11_Shaders.pixel.addTrackingRef (SK_D3D11_Shaders.pixel.blacklist,     PS_CRC32_SHADOWFILTER);
        //else
        //  SK_D3D11_Shaders.pixel.releaseTrackingRef (SK_D3D11_Shaders.pixel.blacklist, PS_CRC32_SHADOWFILTER);

        plugin_ctx._SK_TVFix_SharpenShadows->store (plugin_ctx.__SK_TVFix_SharpenShadows);
      }

      ImGui::TreePop ();
    }

    ImGui::EndGroup   ();
    ImGui::SameLine   ();
    ImGui::BeginGroup ();

    const bool tex_manage =
      ImGui::CollapsingHeader ("Texture Management##ToV", ImGuiTreeNodeFlags_DefaultOpen);

    //bool changed = false;

    if (tex_manage)
    {
      ImGui::TreePush    ("");
      ImGui::BeginGroup  (  );
    /*changed |=*/ ImGui::Checkbox ("Generate Mipmaps", &config.textures.d3d11.generate_mips);

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip    ();
        ImGui::PushStyleColor  (ImGuiCol_Text, (ImVec4&&)ImColor::HSV (0.5f, 0.f, 1.f, 1.f));
        ImGui::TextUnformatted ("Builds Complete Mipchains (Mipmap LODs) for all Textures");
        ImGui::Separator       ();
        ImGui::PopStyleColor   ();
        ImGui::Bullet          (); ImGui::SameLine ();
        ImGui::PushStyleColor  (ImGuiCol_Text, (ImVec4&&)ImColor::HSV (0.15f, 1.0f, 1.0f));
        ImGui::TextUnformatted ("SIGNIFICANTLY");
        ImGui::PopStyleColor   (); ImGui::SameLine ();
        ImGui::TextUnformatted ("reduces texture aliasing");
        ImGui::BulletText      ("May increase load-times");
        ImGui::EndTooltip      ();
      }

      if (config.textures.d3d11.generate_mips)
      {
        ImGui::SameLine ();

        extern uint64_t SK_D3D11_MipmapCacheSize;

        ///if (ImGui::Checkbox ("Cache Mipmaps to Disk", &config.textures.d3d11.cache_gen_mips))
        ///{
        ///  changed = true;
        ///
        ///  ys8_config.mipmaps.cache->store (config.textures.d3d11.cache_gen_mips);
        ///}
        ///
        ///if (ImGui::IsItemHovered ())
        ///{
        ///  ImGui::BeginTooltip ();
        ///  ImGui::Text         ("Eliminates almost all Texture Pop-In");
        ///  ImGui::Separator    ();
        ///  ImGui::BulletText   ("Will quickly consume a LOT of disk space!");
        ///  ImGui::EndTooltip   ();
        ///}

        static wchar_t wszPath [ MAX_PATH + 2 ] = { };

        if (*wszPath == L'\0')
        {
          extern SK_LazyGlobal <std::wstring> SK_D3D11_res_root;

          wcscpy ( wszPath,
                     SK_EvalEnvironmentVars (SK_D3D11_res_root->c_str ()).c_str () );

          lstrcatW (wszPath, LR"(\inject\textures\MipmapCache\)");
          lstrcatW (wszPath, SK_GetHostApp ());
          lstrcatW (wszPath, LR"(\)");
        }

        if (SK_D3D11_MipmapCacheSize > 0)
        {
              ImGui::SameLine (               );
          if (ImGui::Button   (" Purge Cache "))
          {
            SK_D3D11_MipmapCacheSize -= SK_DeleteTemporaryFiles (wszPath, L"*.dds");

            assert ((int64_t)SK_D3D11_MipmapCacheSize > 0LL);

            void
            WINAPI
            SK_D3D11_PopulateResourceList (bool refresh);

            SK_D3D11_PopulateResourceList (true);
          }

          if (ImGui::IsItemHovered ())
              ImGui::SetTooltip    ("%ws", wszPath);
        }

        //// For safety, never allow a user to touch the final 256 MiB of storage on their device
        //const ULONG FILESYSEM_RESERVE_MIB = 256UL;

        if (SK_D3D11_MipmapCacheSize > 0)
        {
          ImGui::SameLine ();
          ImGui::Text     ("Current Cache Size: %.2f MiB", (double)SK_D3D11_MipmapCacheSize / (1024.0 * 1024.0));
        }

        static uint64_t       last_MipmapCacheSize =          0ULL;
        static ULARGE_INTEGER ulBytesAvailable     = { { 0UL, 0UL } },
                              ulBytesTotal         = { { 0UL, 0UL } };

        //
        // GetDiskFreeSpaceEx has highly unpredictable call overhead
        //
        //   ... so try to be careful with it!
        //
        if (SK_D3D11_MipmapCacheSize != last_MipmapCacheSize)
        {
          last_MipmapCacheSize      = SK_D3D11_MipmapCacheSize;

          GetDiskFreeSpaceEx ( wszPath, &ulBytesAvailable,
                                        &ulBytesTotal, nullptr);
        }

        if (SK_D3D11_MipmapCacheSize > 0)
        {
          ImGui::ProgressBar ( static_cast <float> (static_cast <long double> (ulBytesAvailable.QuadPart) /
                                                    static_cast <long double> (    ulBytesTotal.QuadPart) ),
                                 ImVec2 (-1, 0), (std::string (
                          SK_File_SizeToStringAF (ulBytesAvailable.QuadPart, 2, 3).data ()
                ) + " Remaining Storage Capacity").c_str ()
          );
        }
      }
      ImGui::EndGroup  ();
      ImGui::TreePop   ();
    }

    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.90f, 0.68f, 0.02f, 0.45f));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.90f, 0.72f, 0.07f, 0.80f));
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImVec4 (0.87f, 0.78f, 0.14f, 0.80f));

    if (ImGui::CollapsingHeader ("Gameplay", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::TreePush ("");
      ImGui::Checkbox ("Continue Running in Background###TVFIX_BackgroundRender", &config.window.background_render);

      if (ImGui::IsItemHovered ())
        ImGui::SetTooltip (R"(Only works correctly if the game is set to "Borderless")");

      ImGui::TreePop  (  );
    }

    ImGui::EndGroup (  );

    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.90f, 0.40f, 0.40f, 0.45f));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.90f, 0.45f, 0.45f, 0.80f));
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImVec4 (0.87f, 0.53f, 0.53f, 0.80f));

    if (ImGui::CollapsingHeader ("Debug"))
    {
      ImGui::TreePush   ("");
      ImGui::BeginGroup (  );

      ImGui::Checkbox ("Reduce Microstutter", &plugin_ctx.__SK_TVFix_NoRenderSleep);

      if ( plugin_ctx.instn__model_animation.scanned_addr != nullptr &&
           ImGui::Checkbox ("Enable Model Animation", &plugin_ctx.instn__model_animation.enabled) )
      {
        plugin_ctx.instn__model_animation.enabled = (! plugin_ctx.instn__model_animation.enabled);
        plugin_ctx.instn__model_animation.toggle ();
      }

    //ImGui::SameLine ();

      if ( plugin_ctx.instn__particle_effects.scanned_addr != nullptr &&
           ImGui::Checkbox ("Enable Particle Effects", &plugin_ctx.instn__particle_effects.enabled) )
      {
        plugin_ctx.instn__particle_effects.enabled = (! plugin_ctx.instn__particle_effects.enabled);
        plugin_ctx.instn__particle_effects.toggle ();
      }

      ImGui::Checkbox ("Aspect Ratio Correction", &plugin_ctx.__SK_TVFix_AspectRatioCorrection);

      ImGui::EndGroup (  );
      ImGui::TreePop  (  );
    }

    ImGui::PopStyleColor (9);
    ImGui::TreePop       ( );
  }

  return true;
}




void
SK_TVFix_BeginFrame (void)
{
  auto& plugin_ctx =
    tvfix_ctx.get ();

  // Always run callbacks from the SwapChain thread,
  //   bad stuff will happen if Steam's overlay is loaded
  //     otherwise !!
  SteamAPI_RunCallbacks_Detour ();


  static volatile LONG  __init = 0;
  const SK_RenderBackend   &rb =
    SK_GetCurrentRenderBackend ();

  if (rb.device != nullptr && InterlockedCompareExchange (&__init, 1, 0))
  {
    auto pDev =
      rb.getDevice <ID3D11Device> ();
    SK_ComQIPtr    <IDXGIDevice>
                    pDXGIDev
                       (pDev);

    if (pDXGIDev != nullptr)
    {   pDXGIDev->SetGPUThreadPriority (5); }
  }

  LONG ulFramesDrawn =
    SK_GetFramesDrawn ();

  if (ulFramesDrawn > 30 && ulFramesDrawn < 33)
  {
    extern void SK_ImGui_QueueResetD3D11 (void);
    SK_RunOnce (SK_ImGui_QueueResetD3D11 ());

    if (ulFramesDrawn == 31)
    {
      auto pDevD3D11 =
        rb.getDevice <ID3D11Device> ();

      SK_ComQIPtr <IDXGIDevice>
          pDXGIDev (pDevD3D11);
      if (pDXGIDev != nullptr)
      {
        INT nPrio = 0;
        //pDXGIDev->GetGPUThreadPriority (&nPrio);

        dll_log->Log (L"GPU Priority Was: %li", nPrio);

        //pDev->SetGPUThreadPriority (7);
        //pDev->GetGPUThreadPriority (&nPrio);
        //
        //dll_log.Log (L"GPU Priority Is: %li", nPrio);
      }

      SK_D3D11_DeclHUDShader_Vtx (0xb0831a43);
      SK_D3D11_DeclHUDShader_Vtx (0xf4dac9d5);
    //SK_D3D11_DeclHUDShader_Pix (0x6d243285);

      plugin_ctx.instn__model_animation.scan  ();
      plugin_ctx.instn__particle_effects.scan ();
      plugin_ctx.instn__depth_of_field.scan   ();
      plugin_ctx.instn__blur.scan             ();
      plugin_ctx.instn__bloom.scan            ();

      plugin_ctx._SK_TVFix_DisableDepthOfField =
        _CreateConfigParameterBool ( L"TVFix.Render",
                                     L"DisableDepthOfField",  plugin_ctx.__SK_TVFix_DisableDepthOfField,
                                     L"Disable Depth of Field" );

      plugin_ctx._SK_TVFix_DisableBloom =
        _CreateConfigParameterBool ( L"TVFix.Render",
                                     L"DisableBloom",  plugin_ctx.__SK_TVFix_DisableBloom,
                                     L"Disable Bloom Lighting" );

      plugin_ctx._SK_TVFix_DisableBlur =
        _CreateConfigParameterBool ( L"TVFix.Render",
                                     L"DisableBlur",  plugin_ctx.__SK_TVFix_DisableBlur,
                                     L"Disable Blur" );

      plugin_ctx._SK_TVFix_SharpenShadows =
        _CreateConfigParameterBool ( L"TVFix.Render",
                                    L"SharpenShadows",  plugin_ctx.__SK_TVFix_SharpenShadows,
                                    L"Sharpen Shadows" );

      plugin_ctx._SK_TVFix_ActiveAntiStutter =
        _CreateConfigParameterBool ( L"TVFix.FrameRate",
                                     L"EliminateMicroStutter", plugin_ctx.__SK_TVFix_ActiveAntiStutter,
                                     L"Active Anti-Stutter" );

      if (plugin_ctx.__SK_TVFix_DisableDepthOfField)
      {
        ////instn__depth_of_field.disable ();
        SK_D3D11_Shaders->pixel.addTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x27fbcdeb);
        SK_D3D11_Shaders->pixel.addTrackingRef (SK_D3D11_Shaders->pixel.blacklist, 0x8dfd78fd);
        InterlockedIncrement (&SK_D3D11_DrawTrackingReqs);
      }

      if (plugin_ctx.__SK_TVFix_DisableBloom)
      {
        plugin_ctx.instn__bloom.disable ();
      }

      if (plugin_ctx.__SK_TVFix_DisableBlur)
      {
        plugin_ctx.instn__blur.disable ();
      }

      if (plugin_ctx.__SK_TVFix_SharpenShadows)
      {
        //SK_D3D11_Shaders.pixel.addTrackingRef (SK_D3D11_Shaders.pixel.blacklist, PS_CRC32_SHADOWFILTER);
      }
    }
  }

  plugin_ctx._SK_TVFix_LastKnown_XRes->store ((int)ImGui::GetIO ().DisplaySize.x);
  plugin_ctx._SK_TVFix_LastKnown_YRes->store ((int)ImGui::GetIO ().DisplaySize.y);
}


void
SK_TVFix_CreateTexture2D (
  D3D11_TEXTURE2D_DESC    *pDesc )
{
  // Init the plug-in on first texture upload
  static auto& plugin_ctx =
    tvfix_ctx.get ();

  ///if (pDesc->Width == 16 * (pDesc->Height / 9))
  ///{
  ///  if (pDesc->Height <= __SK_TVFix_LastKnown_YRes / 2)
  ///  {
  ///    pDesc->Width  = __SK_TVFix_LastKnown_XRes / 2;
  ///    pDesc->Height = __SK_TVFix_LastKnown_YRes / 2;
  ///  }
  ///
  ///  else
  ///  {
  ///    pDesc->Width  = __SK_TVFix_LastKnown_XRes;
  ///    pDesc->Height = __SK_TVFix_LastKnown_YRes;
  ///  }
  ///}

  if (pDesc->SampleDesc.Count > 1)
  {
    pDesc->SampleDesc.Count =
      ( config.render.dxgi.msaa_samples != SK_NoPreference ?
        config.render.dxgi.msaa_samples : pDesc->SampleDesc.Count );
  }
}

bool
SK_TVFix_DrawHandler_D3D11 (ID3D11DeviceContext* pDevCtx, SK_TLS* pTLS = nullptr, INT d_idx = -1)
{
  UNREFERENCED_PARAMETER (pDevCtx);
  UNREFERENCED_PARAMETER (pTLS);
  UNREFERENCED_PARAMETER (d_idx);

  return false;
}

bool
SK_EpsilonTest (float x1, float x2, float tolerance = 0.000001f)
{
  if (x1 == x2)
    return true;

  if ( x1 <= (x2 + tolerance) &&
       x1 >= (x2 - tolerance) )
    return true;

  return false;
}

bool
STDMETHODCALLTYPE
SK_TVFix_D3D11_RSSetViewports_Callback (
        ID3D11DeviceContext *This,
        UINT                 NumViewports,
  const D3D11_VIEWPORT      *pViewports )
{
  auto& plugin_ctx =
    tvfix_ctx.get ();

  if (NumViewports == 1 && plugin_ctx.__SK_TVFix_AspectRatioCorrection)
  {
    D3D11_VIEWPORT vp = *pViewports;

    if (SK_EpsilonTest (vp.Width, 16.0f * ((float)plugin_ctx.__SK_TVFix_LastKnown_YRes / 9.0f), 2.0f) &&
        SK_EpsilonTest (vp.Height,         (float)plugin_ctx.__SK_TVFix_LastKnown_YRes,         2.0f))
    {
      float expected =
        vp.Width;
      float actual   =
        (float)plugin_ctx.__SK_TVFix_LastKnown_XRes;

      vp.Width = actual;

      if (SK_EpsilonTest (vp.TopLeftX, (actual - expected) / 2.0f, 2.0f/9.0f))
        vp.TopLeftX = 0.0f;
    }

    if (config.system.log_level > 1)
    {
      dll_log->Log ( L"  VP0 -- (%8.3f x %8.3f) | <%5.2f, %5.2f> | [%3.1f - %3.1f]",
                    pViewports->Width,    pViewports->Height,
                    pViewports->TopLeftX, pViewports->TopLeftY,
                    pViewports->MinDepth, pViewports->MaxDepth );
    }

    This->RSSetViewports (1, &vp);

    return true;
  }

  return false;
}


bool
STDMETHODCALLTYPE
SK_TVFix_D3D11_RSSetScissorRects_Callback (
        ID3D11DeviceContext *This,
        UINT                 NumRects,
  const D3D11_RECT          *pRects )
{
  auto& plugin_ctx =
    tvfix_ctx.get ();

  if (NumRects == 1 && plugin_ctx.__SK_TVFix_AspectRatioCorrection)
  {
    LONG sixteen_by_nine_width = LONG (
      16.0f * ((float)plugin_ctx.__SK_TVFix_LastKnown_YRes / 9.0f)
    );
    LONG sixteen_by_nine_height = LONG (
      plugin_ctx.__SK_TVFix_LastKnown_YRes//9.0f * ((float)__SK_TVFix_LastKnown_XRes / 16.0f)
    );

    LONG overwidth_half_adjust =
      ( plugin_ctx.__SK_TVFix_LastKnown_XRes - sixteen_by_nine_width ) / 2;

    D3D11_RECT rect = *pRects;

    if (                rect.left   == 0                             &&
        SK_EpsilonTest (rect.right,
        sixteen_by_nine_width,  2.0f) &&
        SK_EpsilonTest (rect.bottom,   sixteen_by_nine_height, 2.0f) &&
                        rect.top    == 0 )
    {
      rect.right = plugin_ctx.__SK_TVFix_LastKnown_XRes;
    }

    if (SK_EpsilonTest (rect.left,   overwidth_half_adjust,  2.0f) &&
        SK_EpsilonTest (rect.right,  sixteen_by_nine_width,  2.0f) &&
        SK_EpsilonTest (rect.bottom, sixteen_by_nine_height, 2.0f) &&
        SK_EpsilonTest (rect.top,    0,                      2.0f))
    {
      rect.right = plugin_ctx.__SK_TVFix_LastKnown_XRes;
      rect.left  =    0;
    }

    if (config.system.log_level > 1)
    {
      dll_log->Log ( L"  Scissor0 -- (%li - %li), (%li - %li)",
                    rect.left,    rect.right,
                    rect.bottom,  rect.top );
    }

    This->RSSetScissorRects (1, &rect);

    return true;
  }

  return false;
}

bool SK_TVFix_NoRenderSleep (void)
{
  return tvfix_ctx->__SK_TVFix_NoRenderSleep;
}

bool SK_TVFix_SharpenShadows (void)
{
  return tvfix_ctx->__SK_TVFix_SharpenShadows;
}

bool SK_TVFix_ActiveAntiStutter (void)
{
    return tvfix_ctx->__SK_TVFix_ActiveAntiStutter;
}

#pragma warning (pop)