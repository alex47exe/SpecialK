﻿//
// Copyright 2017-2023  No "Redacted" Name,
//                      Niklas "DrDaxxy"  Kielblock,
//                      Peter  "Durante"  Thoman
//
//        Francesco149, Idk31, Smithfield, emoose,
//          Lilium and GitHub contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sub license, and/or
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


#include <SpecialK/plugin/plugin_mgr.h>
#include <SpecialK/plugin/nier.h>
#include <SpecialK/render/dxgi/dxgi_hdr.h>
#include <SpecialK/render/d3d11/d3d11_core.h>



#define FAR_VERSION_NUM L"0.10.3"
#define FAR_VERSION_STR L"FAR v " FAR_VERSION_NUM

// Block until update finishes, otherwise the update dialog
//   will be dismissed as the game crashes when it tries to
//     draw the first frame.
volatile LONG   __FAR_init        = FALSE;
         float  __FAR_MINIMUM_EXT = 0.0F;
         bool   __FAR_Freelook    = false;
         bool   __FAR_FastLoads   = false;

extern float __target_fps;

#define WORKING_FPS_UNCAP
#define WORKING_CAMERA_CONTROLS
#define WORKING_GAMESTATES
#define WORKING_FPS_SLEEP_FIX


#define mbegin(addr, len)   \
  VirtualProtect (          \
    addr,                   \
    len,                    \
    PAGE_EXECUTE_READWRITE, \
    &old_protect_mask       \
);

#define mend(addr, len)  \
  VirtualProtect (       \
    addr,                \
    len,                 \
    old_protect_mask,    \
    &old_protect_mask    \
);


struct far_game_state_s
{
  uintptr_t
         uiBaseAddr   = 0x0;

  DWORD* pMenu        = nullptr;
  DWORD* pLoading     = nullptr;
  DWORD* pLoading2    = nullptr;
  DWORD* pHacking     = nullptr;
  DWORD* pShortcuts   = nullptr;

  float* pHUDOpacity  = nullptr;

  int    present_rate = 1;     // VSYNC
  bool   fast_loading = false; // Limiter's suspended to make loads faster
  bool   capped       = true;  // Actual state of limiter
  bool   enforce_cap  = true;  // User's current preference
  bool   patchable    = false; // True only if the memory addresses can be validated
  bool   bSteam       = false;
  bool   bSteam2021   = false;

  bool isInMenu   (void) { return ( pMenu      != nullptr && (*pMenu      & 0x1) != 0 ); }
  bool isLoading  (void) { return ( pLoading   != nullptr && (*pLoading   & 0x1) != 0 ); }
  bool isHacking  (void) { if   (! isSteam2021 ())
                           return ( pHacking   != nullptr && (*pHacking   & 0x1) != 0 );
                           return ( pHacking   != nullptr && (*pHacking   & 0x2) != 0 );
    }
  bool isShorting (void) { return ( pShortcuts != nullptr && (*pShortcuts & 0x1) != 0 ); }

  bool needFPSCap (void) {
    if (! patchable)
      return true;

    if (enforce_cap)
      return true;

    __try {
      if (__FAR_FastLoads && isLoading ())
      {
        return false;
      }

      if (game_state.isSteam2021 ())
      {
        DWORD* pCursorVisible =
          (DWORD *)(uiBaseAddr + 0x0F82E38);

        void **ppHacking =
          (void **)(uiBaseAddr + 0x0176D480);

        pHacking =
          (DWORD *)*ppHacking + 0x4B;

        __try
        {
          if (*pCursorVisible || isHacking ())
            return true;
        }

        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
      }

      return
        ( isInMenu () || isShorting () );
    }

    __except (EXCEPTION_EXECUTE_HANDLER)
    {
      patchable = false;

      return true;
    }
  }

  void capFPS   (void);
  void uncapFPS (void);

  void initForSteam (void)
  {
    bSteam          = true;
    bSteam2021      = false;
    __FAR_FastLoads = false;

    // Game state addresses courtesy of Francesco149
    pMenu        = reinterpret_cast <DWORD *> (0x14190D494); //0x1418F39C4;
    pLoading     = reinterpret_cast <DWORD *> (0x14198F0A0); //0x141975520;
    pHacking     = reinterpret_cast <DWORD *> (0x1410FA090); //0x1410E0AB4;
    pShortcuts   = reinterpret_cast <DWORD *> (0x141415AC4); //0x1413FC35C;

    pHUDOpacity  = reinterpret_cast <float *> (0x1419861BC); //0x14196C63C;
  }

  void initForMicrosoft (void)
  {
    bSteam          = false;
    bSteam2021      = false;
    __FAR_FastLoads = true;

    uiBaseAddr =
      reinterpret_cast <uintptr_t> (
        SK_Debug_GetImageBaseAddr ()
      );

    pMenu      = reinterpret_cast <DWORD *> (uiBaseAddr + 0x181A46C);
    pLoading   = reinterpret_cast <DWORD *> (uiBaseAddr + 0x146AEC0);

    pHacking   = nullptr;
    pShortcuts = nullptr;
  //pHacking   = reinterpret_cast <DWORD *> (uiBaseAddr + 0x181A46C /* WRONG */);
  //pShortcuts = reinterpret_cast <DWORD *> (uiBaseAddr + 0x181A408);
  }

  void initForSteam2021 (void);

  bool isSteam     (void) const { return bSteam;     }
  bool isSteam2021 (void) const { return bSteam2021; }
  bool isMicrosoft (void) const { return
   ! ( isSteam     ()
    || isSteam2021 () );
  }

  struct {
    struct {
      uint8_t spinlock_bytes [2] = { 0x0, 0x0                     };
      uint8_t sleep_bytes    [6] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
    } enable, disable;
  } fps_limit;
} static game_state;

struct far_lod_patch_s {
  typedef void*    (* sub_84CD60_Fn) (void* a1, void* a2, void* a3, void* a4,
                                      void* a5, void* a6, void* a7, void* a8);
  typedef uint32_t (* IsAOAllowed_Fn)(void* a1);

  static constexpr uintptr_t HookAddr              = 0x084CD60;
  static constexpr uintptr_t Hook2Addr             = 0x084D070;
  static constexpr uintptr_t SettingAddr_AOEnabled = 0x1421F58;
  static constexpr uintptr_t IsAOAllowedAddr       = 0x078BC20;

  static sub_84CD60_Fn  sub_84CD60_Orig;
  static sub_84CD60_Fn  sub_84D070_Orig;
  static IsAOAllowed_Fn IsAOAllowed_Orig;

#pragma pack(push, 1)
  struct NA_Mesh
  {
    /* 0x000 */ uint8_t  Unk000 [0x390];
    /* 0x390 */ void*    ShadowArray;             // some kind of array/vector related with shadows, "ShadowCast" flag affects something in the entries
    /* 0x398 */ uint8_t  Unk398 [0x058];
    /* 0x3F0 */ float*   DistRates;               // pointer to "DistRate0"-"DistRate3"
    /* 0x3F8 */ uint32_t NumDistRates;
    /* 0x3FC */ float    Unk3FC;
    /* 0x400 */ uint32_t Unk400;                  // "UseLostLOD", gets set if using dist rates?
    /* 0x404 */ uint32_t Unk404;                  // if set, Unk3FC = 0.05 ?
    /* 0x408 */ uint8_t  Unk408 [0x118];
    /* 0x520 */ uint32_t Unk520;                  // "UseCullAABB" sets/removes 0x10 flag
    /* 0x524 */ float    Unk524;
    /* 0x528 */ uint8_t  Unk528 [0x048];
    /* 0x570 */ uint32_t Unk570;                  // "CamAlpha" sets to 1 or 0
    /* 0x574 */ float    Unk574;
    /* 0x578 */ float    Unk578;                  // "CamAlpha"
    /* 0x57C */ float    Unk57C;
    /* 0x580 */ uint32_t Unk580;
    /* 0x584 */ uint32_t AmbientOcclusionAllowed; // "AO_OFF" sets to 0
    /* 0x588 */ uint32_t Unk588;
    /* 0x58C */ uint32_t Unk58C;
    /* 0x590 */ uint8_t  Unk590 [0x058];
    /* 0x5E8 */ float    BloomStrength;           // always 0 or 1 ?

    void DisableLODs (void)
    {
      // Set all DistRates to 0
      if (DistRates != nullptr)
      {
        DistRates [0] = 0.0f;
        DistRates [1] = 0.0f;
        DistRates [2] = 0.0f;
        DistRates [3] = 0.0f;
      }

      // Set number of DistRates to 1 (0 causes weird issues)
      NumDistRates = 1;

      // Disable UseLostLOD
      Unk400 = 0;
      Unk404 = 0;

      // Remove UseCullAABB flag
      Unk520 &= 0xFFFFFFEF;
    }
  };
  static_assert(sizeof(NA_Mesh) == 0x5EC); // not proper size lol
#pragma pack(pop)

  static volatile bool _ApplyPatch;

  static void*
  sub_84CD60 (NA_Mesh* a1, BYTE* a2, void* a3, void* a4,
                 void* a5, void* a6, void* a7, void* a8)
  {
    // In case something in orig function depends on LOD details, disable first
    if (_ApplyPatch) a1->DisableLODs ();

    auto ret =
      sub_84CD60_Orig (a1, a2, a3, a4, a5, a6, a7, a8);

    // Make sure LOD data is disabled
    if (_ApplyPatch) a1->DisableLODs ();

    return ret;
  }

  static void*
  sub_84D070 (NA_Mesh* a1, BYTE* a2, void* a3, void* a4,
                 void* a5, void* a6, void* a7, void* a8)
  {
    // In case something in orig function depends on LOD details, disable first
    if (_ApplyPatch) a1->DisableLODs ();

    auto ret =
      sub_84D070_Orig (a1, a2, a3, a4, a5, a6, a7, a8);

    // Make sure LOD data is disabled
    if (_ApplyPatch) a1->DisableLODs ();

    return ret;
  }

  static uint32_t
  IsAOAllowed (void* a1)
  {
    if (! IsAOAllowed_Orig (a1))
      return 0;

    else if (! _ApplyPatch)
      return 1;

    auto result =
      *(uint32_t *)(game_state.uiBaseAddr + SettingAddr_AOEnabled) != 0;

    return result;
  }

  static bool init (void)
  {
    if (! game_state.isSteam2021 ())
      return false;

    void *pHookTarget        = (void *)(game_state.uiBaseAddr +        HookAddr);
    void *pHook2Target       = (void *)(game_state.uiBaseAddr +       Hook2Addr);
    void *pIsAOAllowedTarget = (void *)(game_state.uiBaseAddr + IsAOAllowedAddr);

    MH_STATUS status =
      SK_CreateFuncHook  (                       L"sub_84CD60",
                                                  pHookTarget,
                                  far_lod_patch_s::sub_84CD60,
         static_cast_p2p <void> (&far_lod_patch_s::sub_84CD60_Orig) );

    if (status != MH_OK)
      return false;

    status =
      SK_CreateFuncHook  (                       L"sub_84D070",
                                                  pHook2Target,
                                  far_lod_patch_s::sub_84D070,
         static_cast_p2p <void> (&far_lod_patch_s::sub_84D070_Orig) );

    if (status != MH_OK)
      return false;

    status =
      SK_CreateFuncHook  (                       L"IsAOAllowed",
                                                  pIsAOAllowedTarget,
                                  far_lod_patch_s::IsAOAllowed,
         static_cast_p2p <void> (&far_lod_patch_s::IsAOAllowed_Orig) );

    if (status != MH_OK)
      return false;

    MH_QueueEnableHook (       pHookTarget);
    MH_QueueEnableHook (      pHook2Target);
    MH_QueueEnableHook (pIsAOAllowedTarget);

    return true;
  }
};

far_lod_patch_s::sub_84CD60_Fn  far_lod_patch_s::sub_84CD60_Orig  = nullptr;
far_lod_patch_s::sub_84CD60_Fn  far_lod_patch_s::sub_84D070_Orig  = nullptr;
far_lod_patch_s::IsAOAllowed_Fn far_lod_patch_s::IsAOAllowed_Orig = nullptr;
volatile bool                   far_lod_patch_s::_ApplyPatch      = false;


SK_LazyGlobal <sk::ParameterFactory>
                      far_factory;
iSK_INI*              far_prefs                     = nullptr;
wchar_t               far_prefs_file [MAX_PATH + 2] = {     };
sk::ParameterInt*     far_gi_workgroups             = nullptr;
sk::ParameterFloat*   far_gi_min_light_extent       = nullptr;
sk::ParameterInt*     far_bloom_width               = nullptr;
sk::ParameterBool*    far_bloom_disable             = nullptr;
sk::ParameterBool*    far_fix_motion_blur           = nullptr;
sk::ParameterInt*     far_bloom_skip                = nullptr;
sk::ParameterInt*     far_ao_width                  = nullptr;
sk::ParameterInt*     far_ao_height                 = nullptr;
sk::ParameterBool*    far_ao_disable                = nullptr;
//sk::ParameterBool*    far_limiter_busy              = nullptr;
sk::ParameterBool*    far_uncap_fps                 = nullptr;
sk::ParameterBool*    far_fastload                  = nullptr;
sk::ParameterBool*    far_rtss_warned               = nullptr;
sk::ParameterBool*    far_osd_disclaimer            = nullptr;
sk::ParameterBool*    far_accepted_license          = nullptr;
sk::ParameterStringW* far_hudless_binding           = nullptr;
sk::ParameterStringW* far_center_lock               = nullptr;
sk::ParameterStringW* far_focus_lock                = nullptr;
sk::ParameterStringW* far_free_look                 = nullptr;
sk::ParameterBool*    far_lod_fix                   = nullptr;


static D3D11Dev_CreateBuffer_pfn             _D3D11Dev_CreateBuffer_Original              = nullptr;
static D3D11Dev_CreateShaderResourceView_pfn _D3D11Dev_CreateShaderResourceView_Original  = nullptr;
static D3D11Dev_CreateTexture2D_pfn          _D3D11Dev_CreateTexture2D_Original           = nullptr;
static D3D11_DrawIndexed_pfn                 _D3D11_DrawIndexed_Original                  = nullptr;
static D3D11_Draw_pfn                        _D3D11_Draw_Original                         = nullptr;


struct
{
  bool        enqueue = false; // Trigger a Steam screenshot
  int         clear   = 4;     // Reset enqueue after 3 frames
  float       opacity = 1.0F;  // Original HUD opacity

  SK_Keybind  keybind = {
    "HUD Free Screenshot", L"Num -",
     false, false, false, VK_OEM_MINUS
  };
} static __FAR_HUDLESS;

struct far_cam_state_s
{
  SK_Keybind freelook_binding = {
    "Toggle Camera Freelook Mode", L"Num 5",
      false, false, false, VK_NUMPAD5
  };

  // Memory addresses courtesy of Idk31 and Smithfield
  //  Ptr at  { F3 44 0F 11 88 74 02 00 00 89 88 84 02 00 00 }  +  4
  vec3_t* pCamera    = reinterpret_cast <vec3_t *> (0x141605400);//0x1415EB950;
  vec3_t* pLook      = reinterpret_cast <vec3_t *> (0x141605410);//0x1415EB960;
  float*  pRoll      = reinterpret_cast <float  *> (0x141415B90);//1415EB990;

  vec3_t  fwd   = { 0.0F, 0.0F, 0.0F },
          right = { 0.0F, 0.0F, 0.0F },
          up    = { 0.0F, 0.0F, 0.0F };

  bool center_lock = false,
       focus_lock  = false;

  SK_Keybind center_binding {
     "Camera Center Lock Toggle", L"Num /",
     true, true, false, VK_DIVIDE
  };

  SK_Keybind focus_binding {
     "Camera Focus Lock Toggle", L"Ctrl+Shift+F11",
     true, true, false, VK_F11
  };

  bool toggleCenterLock (void) {
    // { 0x0F, 0xC6, 0xC0, 0x00, 0x0F, 0x5C, 0xF1, 0x0F, 0x59, 0xF0, 0x0F, 0x58, 0xCE }                    -0x7 = Center Lock
    if (center_lock) SK_GetCommandProcessor ()->ProcessCommandLine ("mem l 4d5729 0F0112FCD00D290F");
    else             SK_GetCommandProcessor ()->ProcessCommandLine ("mem l 4d5729 0F90909090909090");

    //4cdc89

    return (center_lock = (! center_lock));
  }

  bool toggleFocusLock (void)
  {
    // Center Lock - C1
    if (focus_lock) SK_GetCommandProcessor ()->ProcessCommandLine ("mem l 4D5668 850112FDA10D290F");
    else            SK_GetCommandProcessor ()->ProcessCommandLine ("mem l 4D5668 8590909090909090");

    return (focus_lock = (! focus_lock));
  }
} static far_cam;

// (Presumable) Size of compute shader workgroup
UINT   __FAR_GlobalIllumWorkGroupSize =   128;
bool   __FAR_GlobalIllumCompatMode    =  true;

struct {
  int  width   =    -1; // Set at startup from user prefs, never changed
  int  skip    =     0;

  bool disable = false;
  bool active  = false;
} far_bloom;

struct {
  int  width           =    -1; // Set at startup from user prefs, never changed
  int  height          =    -1; // Set at startup from user prefs, never changed

  bool active          = false;

  bool disable         = false;
  bool fix_motion_blur = true;
} far_ao;

bool                      SK_FAR_PlugInCfg         (void);

HRESULT
WINAPI
SK_FAR_CreateBuffer (
  _In_           ID3D11Device            *This,
  _In_     const D3D11_BUFFER_DESC       *pDesc,
  _In_opt_ const D3D11_SUBRESOURCE_DATA  *pInitialData,
  _Out_opt_      ID3D11Buffer           **ppBuffer )
{
  D3D11_SUBRESOURCE_DATA new_data =
    ( (pInitialData != nullptr)  ?  (*pInitialData)  :
                                      D3D11_SUBRESOURCE_DATA { } );

  D3D11_BUFFER_DESC      new_desc =
    ( (pDesc        != nullptr)  ?  (*pDesc)         :
                                      D3D11_BUFFER_DESC      { } );


  struct far_light_volume_s {
    float world_pos    [ 4];
    float world_to_vol [16];
    float half_extents [ 4];
  } static new_lights  [128] = { };

  // Global Illumination (DrDaxxy)
  if ( pDesc != nullptr && pDesc->StructureByteStride == sizeof (far_light_volume_s)       &&
                           pDesc->ByteWidth           == sizeof (far_light_volume_s) * 128 &&
                           (pDesc->BindFlags           & D3D11_BIND_SHADER_RESOURCE) ==
                                                         D3D11_BIND_SHADER_RESOURCE )
  {
    new_desc.ByteWidth =
      sizeof (far_light_volume_s) * std::max (1U, __FAR_GlobalIllumWorkGroupSize);

    // New Stuff for 0.6.0
    // -------------------
    //
    //  >> Project small lights to infinity and leave large lights lit <<
    //
    if (pInitialData != nullptr && pInitialData->pSysMem != nullptr)
    {
      auto* lights =
        static_const_cast <far_light_volume_s *, void *> (pInitialData->pSysMem);

      ZeroMemory ( new_lights, pDesc->ByteWidth );

      if (__FAR_GlobalIllumWorkGroupSize != 0)
      {
        CopyMemory ( new_lights,
                         lights,
             sizeof (far_light_volume_s) * __FAR_GlobalIllumWorkGroupSize );

        if (game_state.isSteam () || game_state.isSteam2021 ())
        {
          // This code is bloody ugly, but it works ;)
          for (UINT i = 0; i < std::min (__FAR_GlobalIllumWorkGroupSize, 128U); i++)
          {
            float light_pos [4] = { lights [i].world_pos [0], lights [i].world_pos [1],
                                    lights [i].world_pos [2], lights [i].world_pos [3] };

            glm::vec4   cam_pos_world ( light_pos [0] - (reinterpret_cast <float *> (far_cam.pCamera)) [0],
                                        light_pos [1] - (reinterpret_cast <float *> (far_cam.pCamera)) [1],
                                        light_pos [2] - (reinterpret_cast <float *> (far_cam.pCamera)) [2],
                                                     1.0f );

            glm::mat4x4 world_mat ( lights [i].world_to_vol [ 0], lights [i].world_to_vol [ 1], lights [i].world_to_vol [ 2], lights [i].world_to_vol [ 3],
                                    lights [i].world_to_vol [ 4], lights [i].world_to_vol [ 5], lights [i].world_to_vol [ 6], lights [i].world_to_vol [ 7],
                                    lights [i].world_to_vol [ 8], lights [i].world_to_vol [ 9], lights [i].world_to_vol [10], lights [i].world_to_vol [11],
                                    lights [i].world_to_vol [12], lights [i].world_to_vol [13], lights [i].world_to_vol [14], lights [i].world_to_vol [15] );

            glm::vec4   test = world_mat * cam_pos_world;

            if ( ( fabs (lights [i].half_extents [0]) <= fabs (test.x) * __FAR_MINIMUM_EXT ||
                   fabs (lights [i].half_extents [1]) <= fabs (test.y) * __FAR_MINIMUM_EXT ||
                   fabs (lights [i].half_extents [2]) <= fabs (test.z) * __FAR_MINIMUM_EXT )  /* && ( fabs (lights [i].half_extents [0]) > 0.0001f ||
                                                                                                      fabs (lights [i].half_extents [1]) > 0.0001f ||
                                                                                                      fabs (lights [i].half_extents [2]) > 0.0001f ) */ )
            {
              // Degenerate light volume
              new_lights [i].half_extents [0] = 0.0F;
              new_lights [i].half_extents [1] = 0.0F;
              new_lights [i].half_extents [2] = 0.0F;

              // Project to infinity (but not beyond, because that makes no sense)
              new_lights [i].world_pos [0] = 0.0F; new_lights [i].world_pos [1] = 0.0F;
              new_lights [i].world_pos [2] = 0.0F; new_lights [i].world_pos [3] = 0.0F;
            }
          }
        }
      }

      new_data.pSysMem = static_cast <void *> (new_lights);

      pInitialData = &new_data;
    }

    pDesc = &new_desc;
  }

  return
    _D3D11Dev_CreateBuffer_Original (This, pDesc, pInitialData, ppBuffer);
}

HRESULT
WINAPI
SK_FAR_CreateShaderResourceView (
  _In_           ID3D11Device                     *This,
  _In_           ID3D11Resource                   *pResource,
  _In_opt_ const D3D11_SHADER_RESOURCE_VIEW_DESC  *pDesc,
  _Out_opt_      ID3D11ShaderResourceView        **ppSRView )
{
  D3D11_SHADER_RESOURCE_VIEW_DESC new_desc =
    ( pDesc != nullptr ?
                *pDesc : D3D11_SHADER_RESOURCE_VIEW_DESC { } );

  // Global Illumination (DrDaxxy)
  if ( pDesc != nullptr && pDesc->ViewDimension        == D3D_SRV_DIMENSION_BUFFEREX &&
                           pDesc->BufferEx.NumElements == 128 )
  {
    if (! __FAR_GlobalIllumCompatMode)
      return D3D11Dev_CreateShaderResourceView_Original (This, pResource, pDesc, ppSRView);

    SK_ComPtr <ID3D11Buffer> pBuf;

    if ( SUCCEEDED (
           pResource->QueryInterface <ID3D11Buffer> (&pBuf)
         )
       )
    {
      D3D11_BUFFER_DESC buf_desc;

      pBuf->GetDesc (&buf_desc);

      if (buf_desc.ByteWidth    == 96 * std::max (1U, __FAR_GlobalIllumWorkGroupSize))
        new_desc.BufferEx.NumElements = std::max (1U, __FAR_GlobalIllumWorkGroupSize);

      pDesc = &new_desc;
    }
  }

  HRESULT hr =
    _D3D11Dev_CreateShaderResourceView_Original (This, pResource, pDesc, ppSRView);

  return hr;
}


enum class SK_FAR_WaitBehavior
{
  Sleep = 0x1,
  Busy  = 0x2
};

SK_FAR_WaitBehavior wait_behavior (SK_FAR_WaitBehavior::Sleep);

void
SK_FAR_SetFramerateCap (bool enable)
{
  if (enable)
  {
    game_state.enforce_cap =  false;
    far_uncap_fps->set_value (true);
  } else {
    far_uncap_fps->set_value (false);
    game_state.enforce_cap =  true;
  }
}

// Altimor's FPS cap removal
//
uint8_t* psleep     = reinterpret_cast <uint8_t *> (0x14092E887); // Original pre-patch
uint8_t* pspinlock  = reinterpret_cast <uint8_t *> (0x14092E8CF); // +0x48
uint8_t* pmin_tstep = reinterpret_cast <uint8_t *> (0x140805DEC); // Original pre-patch
uint8_t* pmax_tstep = reinterpret_cast <uint8_t *> (0x140805E18); // +0x2c

bool
SK_FAR_SetLimiterWait (SK_FAR_WaitBehavior behavior)
{
                                          // Bytecode                         //  Offset
  static uint8_t sleep_wait_steam     [] = { 0x7e, 0x08, 0x8b, 0xca, 0xff, 0x15, { 0x48 } };
  static uint8_t sleep_wait_steam2021 [] = { 0x85, 0xc9, 0x7e, 0x06, 0xff, 0x15, { 0x4A } };
  static uint8_t busy_wait            [] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90        };

  static bool init = false;

  // Square-Enix rarely patches the games they publish, so just search for this pattern and
  //   don't bother to adjust memory addresses... if it's not found using the hard-coded address,
  //     bail-out.
  if (! init)
  {
    init = true;

    uint8_t *pSleepPattern =
      game_state.isSteam     () ? sleep_wait_steam     :
      game_state.isSteam2021 () ? sleep_wait_steam2021 : nullptr;

    psleep =
      pSleepPattern == nullptr ?
                       nullptr :
      static_cast <uint8_t *> (SK_ScanAligned ( pSleepPattern, 6, nullptr, 1 ));

    if ( psleep == nullptr )
    {
      dll_log->Log (L"[ FARLimit ]  Could not locate Framerate Limiter Sleep Addr.");

      pspinlock  = nullptr;
      pmin_tstep = nullptr;
      pmax_tstep = nullptr;
    }

    else
    {
      ptrdiff_t spinlock_offset =
          pSleepPattern [6];

      psleep += 4;

      dll_log->Log (L"[ FARLimit ]  Scanned Framerate Limiter Sleep Addr.: 0x%p", psleep);

      // Backup the original instructions
      memcpy (pSleepPattern, psleep, 6);

      // Relative adddress of spinlock
      pspinlock = psleep + spinlock_offset;

      if (game_state.isSteam ())
      {
        uint8_t tstep0      [] = { 0x73, 0x1C, 0xC7, 0x05, 0x00, 0x00 };
        uint8_t tstep0_mask [] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 };

        pmin_tstep = static_cast <uint8_t *> (SK_ScanAligned ( tstep0, sizeof tstep0, tstep0_mask, 1 ));
        pmax_tstep = pmin_tstep + 0x2c;

        if (pmin_tstep != nullptr) // Steam Version
          dll_log->Log (L"[ FARLimit ]  Scanned Framerate Limiter TStepMin Addr.: 0x%p", pmin_tstep);
      }

      else
      {
        pmin_tstep = nullptr;
        pmax_tstep = nullptr;
      }

      //{ 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x24, 0xF3, 0x0F, 0x11, 0x54, 0x24, 0x28, 0xF3, 0x0F, 0x11, 0x5C, 0x24, 0x2C }    (-4) = HUD Opacity
    }
  }

  if (psleep == nullptr)
    return false;

  wait_behavior = behavior;

  DWORD old_protect_mask;

  // Hard coded for now; safe to do this without pausing threads and flushing caches
  //   because the config UI runs from the only thread that the game framerate limits.
  mbegin (psleep, 6);
  switch (behavior)
  {
    case SK_FAR_WaitBehavior::Busy:
      if (game_state.isSteam () || game_state.isSteam2021 ())
                                 memcpy (psleep, busy_wait,            6); break;
    case SK_FAR_WaitBehavior::Sleep:
      if (game_state.isSteam ()) memcpy (psleep, sleep_wait_steam,     6);
      else                       memcpy (psleep, sleep_wait_steam2021, 6); break;
  }
  mend (psleep, 6);

  return true;
}


static SK_EndFrameEx_pfn SK_EndFrameEx_Original = nullptr;

void
STDMETHODCALLTYPE
SK_FAR_EndFrameEx (BOOL bWaitOnFail)
{
  static LONGLONG frames_drawn = 0;

  SK_EndFrameEx_Original (bWaitOnFail);

  if (far_osd_disclaimer->get_value ())
    SK_DrawExternalOSD ( "FAR", "  Press Ctrl + Shift + O         to toggle In-Game OSD\n"
                                "  Press Ctrl + Shift + Backspace to access In-Game Config Menu\n"
                                "    ( Select + Start on Gamepads )\n\n"
                                "   * This message will go away the first time you actually read it and successfully toggle the OSD.\n" );
  else if (config.system.log_level == 1)
  {
    std::string validation;

    if (frames_drawn >= 0 && game_state.needFPSCap ())
    {
      validation += "FRAME: ";

      static char szFrameNum [32] = { '\0' };
      snprintf (szFrameNum, 31, "%lli (%c) ", frames_drawn, 'A' +
                        sk::narrow_cast <int>(frames_drawn++ % 26LL) );

      validation += szFrameNum;
    }

    else //if ((! game_state.needFPSCap ()) || frames_drawn < 0)
    {
      // First offense is last offense
      frames_drawn = -1;

      validation += "*** CHEATER ***";
    }

    SK_DrawExternalOSD ( "FAR", validation );
  }

  else if (config.system.log_level > 1)
  {
    std::string state = "";

    if (game_state.needFPSCap ()) {
      state += "< Needs Cap :";

      std::string reasons;

      if (game_state.isLoading  ())   reasons += " loading ";
      if (game_state.isInMenu   ())      reasons += " menu ";
      if (game_state.isHacking  ())   reasons += " hacking ";
      if (game_state.isShorting ()) reasons += " shortcuts ";

      state += reasons;
      state += ">";
    }

    if (game_state.capped)
      state += " { Capped }";
    else
      state += " { Uncapped }";

    SK_DrawExternalOSD ( "FAR", state);

    if (frames_drawn > 0)
      frames_drawn = -1;
  }

  else
  {
    SK_DrawExternalOSD            ( "FAR", "" );

    if (frames_drawn > 0)
      frames_drawn = -1;
  }

  // Prevent patching an altered executable
  if (game_state.patchable)
  {
    if (game_state.needFPSCap () && (! game_state.capped))
    {
      game_state.capFPS ();
      game_state.capped = true;
    }

    if ((! game_state.needFPSCap ()) && game_state.capped)
    {
      game_state.uncapFPS ();
      game_state.capped = false;
    }


    if (game_state.isSteam2021 ())
    {
      if (far_uncap_fps->get_value ())
      {
        static constexpr auto
          NIER_INTERNAL_FPS = 60;

        static const
          LONGLONG llClockFreq =
                   (SK_PerfFreq / NIER_INTERNAL_FPS);

        const SK_RenderBackend& rb =
          SK_GetCurrentRenderBackend ();

        ULONG64 llDeltaTime =
          rb.frame_delta.getDeltaTime ();

        float scale =
            game_state.isInMenu () ?
                              1.0f : static_cast <float>
                                       ( static_cast <double> (llDeltaTime) /
                                         static_cast <double> (llClockFreq) );

        // Game has sketchy < 30 FPS compensation, we need to not mess with it.
        if (scale >= 1.075f)
            scale  = 1.0f;

        float *fTimeScale =
          (float *)(game_state.uiBaseAddr + 0x1029F88);

        *fTimeScale =
              scale;
      }
    }


    if (__FAR_FastLoads)
    {
      auto pLimiter =
        SK::Framerate::GetLimiter (SK_GetCurrentRenderBackend ().swapchain.p);

      if (game_state.isLoading ())
      {
        if (! game_state.fast_loading)
        {
          pLimiter->suspend ();

          game_state.present_rate =
            config.render.framerate.present_interval;
            config.render.framerate.present_interval = 0;

          game_state.fast_loading = true;
        }
      }

      else
      {
        if (game_state.fast_loading)
        {
          pLimiter->resume ();

          config.render.framerate.present_interval =
                       game_state.present_rate;

          game_state.fast_loading = false;
        }
      }
    }


    if (__FAR_HUDLESS.enqueue)
    {
      if (__FAR_HUDLESS.clear == 4-1)
      {
        // In all truth, I should capture the screenshot myself, but I don't
        //   want to bother with that right now ;)
        SK_SteamAPI_TakeScreenshot ();
        __FAR_HUDLESS.clear--;
      }

      else if (__FAR_HUDLESS.clear <= 0)
      {
        (*game_state.pHUDOpacity) =
          __FAR_HUDLESS.opacity;

        __FAR_HUDLESS.clear   = 4;
        __FAR_HUDLESS.enqueue = false;
      }

      else
        __FAR_HUDLESS.clear--;
    }
  }

  XINPUT_STATE state = { };

  if (__FAR_Freelook && SK_XInput_PollController (0, &state))
  {
    float LX   = state.Gamepad.sThumbLX;
    float LY   = state.Gamepad.sThumbLY;

    float norm = sqrt (LX*LX + LY*LY);
    float unit = 1.0f;

    if (norm > static_cast <float> (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE))
    {
      norm = std::min (norm, 32767.0f) - static_cast <float> (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
      unit =           norm/(32767.0f  - static_cast <float> (XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE));
    }

    else
    {
      norm = 0.0f;
      unit = 0.0f;
    }

    float uLX = (LX / 32767.0f) * unit;
    float uLY = (LY / 32767.0f) * unit;


    float RX   = state.Gamepad.sThumbRX;
    float RY   = state.Gamepad.sThumbRY;

    norm = sqrt (RX*RX + RY*RY);
    unit = 1.0f;

    if (norm > static_cast <float> (XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE))
    {
      norm = std::min (norm, 32767.0f) - static_cast <float> (XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
      unit =           norm/(32767.0f  - static_cast <float> (XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE));
    }

    else
    {
      norm = 0.0f;
      unit = 0.0f;
    }

    float ddX = uLX;
    float ddY = uLY;

    vec3_t pos;     pos     [0] = (*far_cam.pCamera) [0]; pos    [1] = (*far_cam.pCamera) [1]; pos    [2] = (*far_cam.pCamera) [2];
    vec3_t target;  target  [0] = (*far_cam.pLook)   [0]; target [1] = (*far_cam.pLook)   [1]; target [2] = (*far_cam.pLook)   [2];

    vec3_t diff; diff [0] = target [0] - pos [0];
                 diff [1] = target [1] - pos [1];
                 diff [2] = target [2] - pos [2];

    float hypXY = sqrtf (diff [0] * diff [0] + diff [2] * diff [2]);

    float dX, dY, dZ;

    dX = ddX*diff [2]/hypXY;
    dY = ddX*diff [0]/hypXY;

    (*far_cam.pLook) [0]   = target [0] - dX;
    (*far_cam.pLook) [2]   = target [2] + dY;

    (*far_cam.pCamera) [0] = pos    [0] - dX;
    (*far_cam.pCamera) [2] = pos    [2] + dY;

    pos     [0] = (*far_cam.pCamera) [0]; pos    [1] = (*far_cam.pCamera) [1]; pos    [2] = (*far_cam.pCamera) [2];
    target  [0] = (*far_cam.pLook)   [0]; target [1] = (*far_cam.pLook)   [1]; target [2] = (*far_cam.pLook)   [2];

    diff    [0] = target [0] - pos [0];
    diff    [1] = target [1] - pos [1];
    diff    [2] = target [2] - pos [2];

    hypXY = sqrtf (diff [0] * diff [0] + diff [2] * diff [2]);

    dX = ddY * diff [0] / hypXY;
    dY = ddY * diff [2] / hypXY;
    dZ = ddY * diff [1] / hypXY;


    (*far_cam.pLook) [0]   = target [0] + dX;
    (*far_cam.pLook) [2]   = target [2] + dY;

    (*far_cam.pCamera) [0] = pos    [0] + dX;
    (*far_cam.pCamera) [2] = pos    [2] + dY;
  }
}


// Sit and spin until the user figures out what an OSD is
//
DWORD
WINAPI
SK_FAR_OSD_Disclaimer (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  while ((volatile bool&)config.osd.show)
    SK_Sleep (66);

  far_osd_disclaimer->store     (false);

  far_prefs->write              (far_prefs_file);

  SK_CloseHandle (GetCurrentThread ());

  return 0;
}



static SK_PluginKeyPress_pfn SK_PluginKeyPress_Original;

#define SK_MakeKeyMask(vKey,ctrl,shift,alt) \
  static_cast <UINT>((vKey) | (((ctrl) != 0) <<  9) |   \
                              (((shift)!= 0) << 10) |   \
                              (((alt)  != 0) << 11))

#define SK_ControlShiftKey(vKey) SK_MakeKeyMask ((vKey), true, true, false)

void
CALLBACK
SK_FAR_PluginKeyPress (BOOL Control, BOOL Shift, BOOL Alt, BYTE vkCode)
{
  auto uiMaskedKeyCode =
    SK_MakeKeyMask (vkCode, Control, Shift, Alt);

  const auto uiHudlessMask =
    SK_MakeKeyMask ( __FAR_HUDLESS.keybind.vKey,  __FAR_HUDLESS.keybind.ctrl,
                     __FAR_HUDLESS.keybind.shift, __FAR_HUDLESS.keybind.alt );

  const auto uiLockCenterMask =
    SK_MakeKeyMask ( far_cam.center_binding.vKey,  far_cam.center_binding.ctrl,
                     far_cam.center_binding.shift, far_cam.center_binding.alt );

  const auto uiLockFocusMask =
    SK_MakeKeyMask ( far_cam.focus_binding.vKey,  far_cam.focus_binding.ctrl,
                     far_cam.focus_binding.shift, far_cam.focus_binding.alt );

  const auto uiToggleFreelookMask =
    SK_MakeKeyMask ( far_cam.freelook_binding.vKey,  far_cam.freelook_binding.ctrl,
                     far_cam.freelook_binding.shift, far_cam.freelook_binding.alt );

  switch (uiMaskedKeyCode)
  {
#ifdef WORKING_FPS_UNCAP
    case SK_ControlShiftKey (VK_OEM_PERIOD):
      SK_FAR_SetFramerateCap (game_state.enforce_cap);
      break;
#endif

    case SK_ControlShiftKey (VK_OEM_6): // ']'
    {
      if (__FAR_GlobalIllumWorkGroupSize < 8)
        __FAR_GlobalIllumWorkGroupSize = 8;

      __FAR_GlobalIllumWorkGroupSize <<= 1ULL;

      if (__FAR_GlobalIllumWorkGroupSize > 128)
        __FAR_GlobalIllumWorkGroupSize = 128;
    } break;

    case SK_ControlShiftKey (VK_OEM_4): // '['
    {
      if (__FAR_GlobalIllumWorkGroupSize > 128)
        __FAR_GlobalIllumWorkGroupSize = 128;

      __FAR_GlobalIllumWorkGroupSize >>= 1UL;

      if (__FAR_GlobalIllumWorkGroupSize < 16)
        __FAR_GlobalIllumWorkGroupSize = 0;
    } break;

    default:
    {
      if (game_state.pHUDOpacity != nullptr)
      {
        if (uiMaskedKeyCode == uiHudlessMask)
        {
          if (__FAR_HUDLESS.enqueue == false )
          {
            __FAR_HUDLESS.clear     = 4;
            __FAR_HUDLESS.enqueue   = true;
            __FAR_HUDLESS.opacity   = (*game_state.pHUDOpacity);
            *game_state.pHUDOpacity = 0.0f;
          }
        }

        else if (uiMaskedKeyCode == uiLockCenterMask)
        {
          far_cam.toggleCenterLock ();
        }

        else if (uiMaskedKeyCode == uiLockFocusMask)
        {
          far_cam.toggleFocusLock ();
        }

        else if (uiMaskedKeyCode == uiToggleFreelookMask)
        {
          __FAR_Freelook = (! __FAR_Freelook);
        }
      }
    } break;
  }

  SK_PluginKeyPress_Original (Control, Shift, Alt, vkCode);
}


extern void WINAPI SK_PluginKeyPress (BOOL,BOOL,BOOL,BYTE);

HRESULT
STDMETHODCALLTYPE
SK_FAR_PresentFirstFrame (IUnknown* pSwapChain, UINT SyncInterval, UINT Flags)
{
  UNREFERENCED_PARAMETER (pSwapChain);
  UNREFERENCED_PARAMETER (SyncInterval);
  UNREFERENCED_PARAMETER (Flags);

  if (0 == InterlockedCompareExchange (&__FAR_init, 1, 0))
  {
    game_state.enforce_cap = (! far_uncap_fps->get_value ());

    bool busy_wait = true;// far_limiter_busy->get_value ();

    game_state.patchable =
      SK_FAR_SetLimiterWait ( busy_wait ? SK_FAR_WaitBehavior::Busy :
                                          SK_FAR_WaitBehavior::Sleep );

    //
    // Hook keyboard input, only necessary for the FPS cap toggle right now
    //
    SK_CreateFuncHook (      L"SK_PluginKeyPress",
                               SK_PluginKeyPress,
                               SK_FAR_PluginKeyPress,
      static_cast_p2p <void> (&SK_PluginKeyPress_Original) );
    SK_EnableHook        (     SK_PluginKeyPress           );

    if (SK_GetModuleHandle (L"RTSSHooks64.dll"))
    {
      bool warned = far_rtss_warned->get_value ();

      if (! warned)
      {
        warned = true;

        SK_MessageBox ( L"RivaTuner Statistics Server Detected\r\n\r\n\t"
                        L"If FAR does not work correctly, this is probably why.",
                          L"Incompatible Third-Party Software", MB_OK | MB_ICONWARNING );

        far_rtss_warned->store     (true);
        far_prefs->write           (far_prefs_file);
      }
    }

    // Since people don't read guides, nag them to death...
    if (far_osd_disclaimer->get_value ())
    {
      SK_Thread_Create ( SK_FAR_OSD_Disclaimer );
    }
  }

  return S_OK;
}

// Overview (Durante):
//
//  The bloom pyramid in Nier:A is built up of 5 buffers, which are sized
//  800x450, 400x225, 200x112, 100x56 and 50x28, regardless of resolution
//  the mismatch between the largest buffer size and the screen resolution (in e.g. 2560x1440 or even 1920x1080)
//  leads to some really ugly artifacts.
//
//  To change this, we need to
//    1) Replace the rendertarget textures in question at their creation point
//    2) Adjust the viewport and some constant shader parameter each time they are rendered to
//
//  Examples here:
//    http://abload.de/img/bloom_defaultjhuq9.jpg
//    http://abload.de/img/bloom_fixedp7uef.jpg
//
//  Note that there are more low-res 800x450 buffers not yet handled by this,
//  but which could probably be handled similarly. Primarily, SSAO.

// NOTE (Redacted):
//
//  Windows Store version increased the resolution to 960x540, but the general overview remains
//

__declspec (noinline)
HRESULT
WINAPI
SK_FAR_CreateTexture2D (
  _In_            ID3D11Device           *This,
  _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
  _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
  _Out_opt_       ID3D11Texture2D        **ppTexture2D )
{
  //if (! game_state.isSteam ())
  // return
  //  _D3D11Dev_CreateTexture2D_Original ( This,
  //                                        pDesc, pInitialData,
  //                                          ppTexture2D );

  if (ppTexture2D == nullptr)
    return _D3D11Dev_CreateTexture2D_Original ( This, pDesc, pInitialData, nullptr );

  static UINT   resW      = far_bloom.width; // horizontal resolution, must be set at application start
  static double resFactor =
        (double)resW / (
           game_state.isSteam () ?
                          1600.0 : 1920.0 ); // the factor required to scale to the largest part of the pyramid

  bool bloom = false;
  bool ao    = false;

  D3D11_TEXTURE2D_DESC copy (*pDesc);

  ////if (! DirectX::IsCompressed (pDesc->Format))
  ////{
  ////  dll_log->Log (L"Tex2D: (%lux%lu): %s { %s/%s }",
  ////          pDesc->Width,
  ////          pDesc->Height, SK_DXGI_FormatToStr (pDesc->Format)   .c_str (),
  ////                      SK_D3D11_DescribeUsage (pDesc->Usage),
  ////                  SK_D3D11_DescribeBindFlags (pDesc->BindFlags).c_str ());
  ////}

  switch (pDesc->Format)
  {
    // R11G11B10 float textures of these sizes are part of the BLOOM PYRAMID
    // Note: we do not manipulate the 50x28 buffer
    //    -- it's read by a compute shader and the whole screen white level can be off if it is the wrong size
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    {
      if (__SK_HDR_16BitSwap || pDesc->Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      {
        if ( (                (pDesc->Width == 800 && pDesc->Height == 450)
                           || (pDesc->Width == 400 && pDesc->Height == 225)
                           || (pDesc->Width == 200 && pDesc->Height == 112)
                           || (pDesc->Width == 100 && pDesc->Height == 56)
                         /*|| (pDesc->Width == 50 && pDesc->Height == 28)*/ ) ||
            (                ( pDesc->Width == 960 && pDesc->Height == 540)
                           || (pDesc->Width == 480 && pDesc->Height == 268)
                           || (pDesc->Width == 240 && pDesc->Height == 134)
                           || (pDesc->Width == 120 && pDesc->Height == 67)
                         /*|| (pDesc->Width == 60 && pDesc->Height == 67)*/ )
           )
        {
          static int num_r11g11b10_textures = 0;

          num_r11g11b10_textures++;

          if (num_r11g11b10_textures > far_bloom.skip)
          {
            bloom = true;

            SK_LOG2 ( ( L"Bloom Tex (%lux%lu : %lu)",
                          pDesc->Width, pDesc->Height, pDesc->MipLevels ),
                        L"FAR PlugIn" );

            if (far_bloom.width != -1)
            {
              // Scale the upper parts of the pyramid fully
              // and lower levels progressively less
              double pyramidLevelFactor  = game_state.isSteam () ? (static_cast <double> (pDesc->Width) - 50.0) / 750.0
                                                                 : (static_cast <double> (pDesc->Width) - 60.0) / 900.0;
              double scalingFactor       = 1.0 + (resFactor - 1.0) * pyramidLevelFactor;

              copy.Width  = static_cast <UINT> (static_cast <double> (copy.Width)  * scalingFactor);
              copy.Height = static_cast <UINT> (static_cast <double> (copy.Height) * scalingFactor);

              pDesc       = &copy;
            }
          }
        }
      }
    } break;

    // 800x450 R8G8B8A8_UNORM is the buffer used to store the AO result and subsequently blur it
    // 800x450 R32_FLOAT is used to store hierarchical Z information (individual mipmap levels are rendered to)
    //                   and serves as input to the main AO pass
    // 800x450 D24_UNORM_S8_UINT depth/stencil used together with R8G8B8A8_UNORM buffer for something (unclear) later on
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R32_FLOAT:
  //case DXGI_FORMAT_R8_UNORM: // ??
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    {
      if (game_state.isSteam () && pDesc->Width == 800 && pDesc->Height == 450)
      {
        // Skip the first two textures that match this pattern, they are
        //   not related to AO.
        static int num_r32_textures = 0;

        if (pDesc->Format == DXGI_FORMAT_R32_FLOAT)
          num_r32_textures++;

        if ((! far_ao.fix_motion_blur) || (num_r32_textures > 0))
        {
          ao = true;

          if (far_ao.width != -1)
          {
            SK_LOG1 ( ( L"Mip Levels: %lu, Format: %x, (%x:%x:%x)",
                          pDesc->MipLevels,      pDesc->Format,
                          pDesc->CPUAccessFlags, pDesc->Usage,
                          pDesc->MiscFlags ),
                        L"FAR PlugIn" );

            SK_LOG1 ( ( L"AO Buffer (%lux%lu - Fmt: %x",
                          pDesc->Width, pDesc->Height,
                          pDesc->Format ),
                        L"FAR PlugIn" );

            copy.Width  = far_ao.width;
            copy.Height = far_ao.height;

            pDesc = &copy;
          }
        }
      }
    } break;
  }


  HRESULT hr =
    _D3D11Dev_CreateTexture2D_Original ( This,
                                           pDesc, pInitialData,
                                             ppTexture2D );

  return hr;
}


// High level description:
//
//  IF we have
//   - 1 viewport
//   - with the size of one of the 4 elements of the pyramid we changed
//   - and a primary rendertarget of type R11G11B10
//   - which is associated with a texture of a size different from the viewport
//  THEN
//   - set the viewport to the texture size
//   - adjust the pixel shader constant buffer in slot #12 to this format (4 floats):
//     [ 0.5f / W, 0.5f / H, W, H ] (half-pixel size and total dimensions)
bool
SK_FAR_PreDraw (ID3D11DeviceContext* pDevCtx)
{
  if (far_bloom.active)
  {
    far_bloom.active = false;

    //if (far_bloom.disable)
      //return true;
  }

  if (far_ao.active)
  {
    far_ao.active = false;

    //if (far_ao.disable)
      //return true;
  }

  UINT                      numViewports = 0;
  pDevCtx->RSGetViewports (&numViewports, nullptr);

  if (numViewports == 1 && (far_bloom.width != -1 || far_ao.width != -1))
  {
    D3D11_VIEWPORT            vp = { };
    pDevCtx->RSGetViewports
             (&numViewports, &vp);

    //{
    //  SK_ComPtr <ID3D11RenderTargetView> rtView = nullptr;
    //
    //  pDevCtx->OMGetRenderTargets (1,   &rtView, nullptr);
    //
    //  if (rtView)
    //  {
    //    D3D11_RENDER_TARGET_VIEW_DESC
    //                      desc = { };
    //    rtView->GetDesc (&desc);
    //
    //    if (desc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
    //        desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
    //    {
    //      dll_log->Log (L"%fx%f", vp.Width, vp.Height);
    //    }
    //  }
    //}

    if (  (game_state.isSteam () &&
                          (vp.Width == 800.0f && vp.Height == 450.0f)
                       || (vp.Width == 400.0f && vp.Height == 225.0f)
                       || (vp.Width == 200.0f && vp.Height == 112.0f)
                       || (vp.Width == 100.0f && vp.Height == 56.0f )
                       || (vp.Width == 50.0f  && vp.Height == 28.0f )
                       || (vp.Width == 25.0f  && vp.Height == 14.0f ) ) ||
          (               (vp.Width == 960.0f && vp.Height == 540.0f)
                       || (vp.Width == 480.0f && vp.Height == 268.0f)
                       || (vp.Width == 240.0f && vp.Height == 134.0f)
                       || (vp.Width == 120.0f && vp.Height == 67.0f )
                       || (vp.Width == 100.0f && vp.Height == 56.0f ) )
       )
    {
      SK_ComPtr <ID3D11RenderTargetView> rtView = nullptr;

      pDevCtx->OMGetRenderTargets (1,   &rtView, nullptr);

      if (rtView)
      {
        D3D11_RENDER_TARGET_VIEW_DESC desc;

        rtView->GetDesc (&desc);

        if ( desc.Format == DXGI_FORMAT_R11G11B10_FLOAT    || // Bloom
            (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && __SK_HDR_16BitSwap)
                                                           ||
            ( game_state.isSteam () &&
           ( desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM     || // AO
             desc.Format == DXGI_FORMAT_R32_FLOAT )           // AO
            )
           )
        {
          SK_ComPtr <ID3D11Resource> rt = nullptr;

          rtView->GetResource (&rt);

          if (rt != nullptr)
          {
            SK_ComQIPtr <ID3D11Texture2D>
                rttex (rt);
            if (rttex != nullptr)
            {
              D3D11_TEXTURE2D_DESC texdesc = { };
              rttex->GetDesc     (&texdesc);

              if (texdesc.Width != vp.Width)
              {
                // Here we go!
                // Viewport is the easy part

                vp.Width  = static_cast <float> (texdesc.Width);
                vp.Height = static_cast <float> (texdesc.Height);

                // AO
                //   If we are at mip slice N, divide by 2^N
                if (desc.Texture2D.MipSlice > 0)
                {
                  vp.Width  = ( texdesc.Width  /
                                  powf ( 2.0f,
                    static_cast <float> (desc.Texture2D.MipSlice) ) );
                  vp.Height = ( texdesc.Height /
                                  powf ( 2.0f,
                    static_cast <float> (desc.Texture2D.MipSlice) ) );
                }

                pDevCtx->RSSetViewports (1, &vp);

                // The constant buffer is a bit more difficult

                // We don't want to create a new buffer every frame,
                // but we also can't use the game's because they are read-only
                // this just-in-time initialized map is a rather ugly solution,
                // but it works as long as the game only renders from 1 thread (which it does)
                // NOTE: rather than storing them statically here (basically a global) the lifetime should probably be managed

                SK_ComPtr <ID3D11Device> dev;
                pDevCtx->GetDevice     (&dev);

                D3D11_BUFFER_DESC
                  buffdesc;
                  buffdesc.ByteWidth           = 16;
                  buffdesc.Usage               = D3D11_USAGE_IMMUTABLE;
                  buffdesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
                  buffdesc.CPUAccessFlags      = 0;
                  buffdesc.MiscFlags           = 0;
                  buffdesc.StructureByteStride = 16;

                D3D11_SUBRESOURCE_DATA
                           initialdata = { };

                // Bloom
                //   If we are not rendering to a mip map for hierarchical Z, the format is
                //   [ 0.5f / W, 0.5f / H, W, H ] (half-pixel size and total dimensions)
                if (desc.Texture2D.MipSlice == 0 && far_bloom.width != -1)
                {
                  static std::unordered_map <UINT, ID3D11Buffer*> buffers;

                  if (! buffers.count (texdesc.Width))
                  {
                    SK_LOG3 ( ( L"Create Bloom Buffer (%lu)", texdesc.Width ),
                                L"FAR PlugIn" );

                    static float constants [4];
                                 constants [0] = 0.5f / vp.Width;
                                 constants [1] = 0.5f / vp.Height;
                                 constants [2] =        vp.Width;
                                 constants [3] =        vp.Height;

                    initialdata.pSysMem =
                      constants;

                    ID3D11Buffer                                *replacementbuffer = nullptr;
                    dev->CreateBuffer (&buffdesc, &initialdata, &replacementbuffer);

                    buffers [texdesc.Width] =
                      replacementbuffer;
                  }

                  pDevCtx->PSSetConstantBuffers (
                    12, 1, &buffers [texdesc.Width]
                  );

                  if (far_bloom.disable)
                    return true;
                }

                // AO
                //
                //   For hierarchical Z mips, the format is
                //   [ W, H, LOD (Mip-1), 0.0f ]
                else if (game_state.isSteam () && far_ao.width != -1)
                {
                  static std::unordered_map <UINT, ID3D11Buffer*> mipBuffers;

                  if (0 == mipBuffers.count (desc.Texture2D.MipSlice))
                  {
                    SK_LOG3 ( ( L"Create AO Buffer (%lu)", desc.Texture2D.MipSlice ),
                                L"FAR PlugIn" );

                    static float constants [4];
                                 constants [0] = vp.Width;
                                 constants [1] = vp.Height;
                                 constants [2] =
                            static_cast <float> (desc.Texture2D.MipSlice) - 1.0F;
                                 constants [3] = 0.0F;

                    initialdata.pSysMem = constants;

                    ID3D11Buffer                                *replacementbuffer = nullptr;
                    dev->CreateBuffer (&buffdesc, &initialdata, &replacementbuffer);

                    mipBuffers [desc.Texture2D.MipSlice] = replacementbuffer;
                  }

                  pDevCtx->PSSetConstantBuffers (8, 1, &mipBuffers [desc.Texture2D.MipSlice]);

                  if (far_ao.disable)
                    return true;
                }
              }
            }
          }
        }
      }
    }
  }

  return false;
}

__declspec (noinline)
void
WINAPI
SK_FAR_DrawIndexed (
  _In_ ID3D11DeviceContext *This,
  _In_ UINT                 IndexCount,
  _In_ UINT                 StartIndexLocation,
  _In_ INT                  BaseVertexLocation )
{
  bool cull = false;

  if (IndexCount == 4 && StartIndexLocation == 0 && BaseVertexLocation == 0)
    cull = SK_FAR_PreDraw (This);

  if (! cull)
    _D3D11_DrawIndexed_Original ( This, IndexCount,
                                    StartIndexLocation, BaseVertexLocation );
}

__declspec (noinline)
void
WINAPI
SK_FAR_Draw (
  _In_ ID3D11DeviceContext *This,
  _In_ UINT                 VertexCount,
  _In_ UINT                 StartVertexLocation )
{
  bool cull = false;

  if (VertexCount == 4 && StartVertexLocation == 0)
    cull = SK_FAR_PreDraw (This);

  if (! cull)
    _D3D11_Draw_Original ( This, VertexCount,
                             StartVertexLocation );
}


__declspec (noinline)
void
__stdcall
SK_FAR_EULA_Insert (LPVOID reserved)
{
  UNREFERENCED_PARAMETER (reserved);

  if (ImGui::CollapsingHeader ("FAR (Fix Automata Resolution)", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::TextWrapped ( " Copyright 2017-2021  You  \"Know\" Who,\n"
                         "                      Niklas \"DrDaxxy\" Kielblock,\n"
                         "                      Peter  \"Durante\" Thoman\n"
                         "\n"
                         "        Francesco149, Idk31, Smithfield, emoose, and GitHub contributors.\n"
                         "\n"
                         " Permission is hereby granted, free of charge, to any person obtaining a copy\n"
                         " of this software and associated documentation files (the \"Software\"), to\n"
                         " deal in the Software without restriction, including without limitation the\n"
                         " rights to use, copy, modify, merge, publish, distribute, sublicense, and/or\n"
                         " sell copies of the Software, and to permit persons to whom the Software is\n"
                         " furnished to do so, subject to the following conditions:\n"
                         " \n"
                         " The above copyright notice and this permission notice shall be included in\n"
                         " all copies or substantial portions of the Software.\n"
                         "\n"
                         " THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
                         " IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
                         " FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL\n"
                         " THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
                         " LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
                         " FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER\n"
                         " DEALINGS IN THE SOFTWARE.\n" );
  }
}



extern SK_LazyGlobal <
  concurrency::concurrent_vector <d3d11_shader_tracking_s::cbuffer_override_s>
> __SK_D3D11_PixelShader_CBuffer_Overrides;

d3d11_shader_tracking_s::cbuffer_override_s* SK_FAR_CB_Override;

void
SK_FAR_InitPlugin (void)
{
  uint8_t tstep0      [] = { 0x73, 0x1C, 0xC7, 0x05, 0x00, 0x00 };
  uint8_t tstep0_mask [] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 };

  pmin_tstep = static_cast <uint8_t *> (SK_ScanAligned ( tstep0, sizeof tstep0, tstep0_mask, 4 ));
  pmax_tstep = pmin_tstep + 0x2c;

  if (pmin_tstep != nullptr)
                                            game_state.initForSteam     ();
  else if (SK_Steam_GetAppID_NoAPI () == 0) game_state.initForMicrosoft ();
  else                                      game_state.initForSteam2021 ();

  SK_SetPluginName (L"Special K Plug-In :: (" FAR_VERSION_STR ")");

  plugin_mgr->first_frame_fns.emplace (SK_FAR_PresentFirstFrame);
  plugin_mgr->config_fns.emplace      (SK_FAR_PlugInCfg);

  SK_CreateFuncHook (       L"ID3D11Device::CreateBuffer",
                               D3D11Dev_CreateBuffer_Override,
                                 SK_FAR_CreateBuffer,
     static_cast_p2p <void> (&_D3D11Dev_CreateBuffer_Original) );
  MH_QueueEnableHook (         D3D11Dev_CreateBuffer_Override  );

  SK_CreateFuncHook (       L"ID3D11Device::CreateShaderResourceView",
                               D3D11Dev_CreateShaderResourceView_Override,
                                 SK_FAR_CreateShaderResourceView,
     static_cast_p2p <void> (&_D3D11Dev_CreateShaderResourceView_Original) );
  MH_QueueEnableHook (         D3D11Dev_CreateShaderResourceView_Override  );

  SK_CreateFuncHook (       L"ID3D11Device::CreateTexture2D",
                               D3D11Dev_CreateTexture2D_Override,
                                 SK_FAR_CreateTexture2D,
     static_cast_p2p <void> (&_D3D11Dev_CreateTexture2D_Original) );
  MH_QueueEnableHook (         D3D11Dev_CreateTexture2D_Override  );

  SK_CreateFuncHook (       L"ID3D11DeviceContext::Draw",
                               D3D11_Draw_Override,
                              SK_FAR_Draw,
     static_cast_p2p <void> (&_D3D11_Draw_Original) );
  MH_QueueEnableHook (         D3D11_Draw_Override  );

  SK_CreateFuncHook (       L"ID3D11DeviceContext::DrawIndexed",
                               D3D11_DrawIndexed_Override,
                              SK_FAR_DrawIndexed,
     static_cast_p2p <void> (&_D3D11_DrawIndexed_Original) );
  MH_QueueEnableHook (         D3D11_DrawIndexed_Override  );

  LPVOID dontcare = nullptr;

  SK_CreateFuncHook ( L"SK_ImGUI_DrawEULA_PlugIn",
                        SK_ImGui_DrawEULA_PlugIn,
                              SK_FAR_EULA_Insert,
                                &dontcare     );

  MH_QueueEnableHook (SK_ImGui_DrawEULA_PlugIn);

  if (far_prefs == nullptr)
  {
    lstrcatW (far_prefs_file, SK_GetConfigPath ());
    lstrcatW (far_prefs_file, L"FAR.ini");

    far_prefs =
      SK_CreateINI (far_prefs_file);

    far_gi_workgroups =
        dynamic_cast <sk::ParameterInt *>
          (far_factory->create_parameter <int> (L"Global Illumination Compute Shader Workgroups"));

    far_gi_workgroups->register_to_ini ( far_prefs,
                                      L"FAR.Lighting",
                                        L"GlobalIlluminationWorkgroups" );

    if (static_cast <sk::iParameter *> (far_gi_workgroups)->load ())
      __FAR_GlobalIllumWorkGroupSize = far_gi_workgroups->get_value ();

    far_gi_workgroups->store (__FAR_GlobalIllumWorkGroupSize);

    far_gi_min_light_extent =
        dynamic_cast <sk::ParameterFloat *>
          (far_factory->create_parameter <float> (L"Global Illumination Minimum Unclipped Light Volume"));

    far_gi_min_light_extent->register_to_ini ( far_prefs,
                                      L"FAR.Lighting",
                                        L"MinLightVolumeExtent" );

    if (static_cast <sk::iParameter *> (far_gi_min_light_extent)->load ())
      __FAR_MINIMUM_EXT = far_gi_min_light_extent->get_value ();

    far_gi_min_light_extent->store     (__FAR_MINIMUM_EXT);

    far_uncap_fps =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (L"Bypass game's framerate ceiling"));

    far_uncap_fps->register_to_ini ( far_prefs,
                                       L"FAR.FrameRate",
                                         L"UncapFPS" );

    // Disable by default, needs more testing :)
    if (! static_cast <sk::iParameter *> (far_uncap_fps)->load ())
    {
      far_uncap_fps->store (true);

      config.render.framerate.target_fps = 60.0;
                            __target_fps = 60.0;

      config.render.framerate.sleepless_render = true;
      config.render.framerate.sleepless_window = true;
    }

    far_fastload =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (
            L"Kill framerate limits during load") );

    far_fastload->register_to_ini ( far_prefs,
                                      L"FAR.FrameRate",
                                        L"FastLoad" );

    if (! far_fastload->load (__FAR_FastLoads))
    {
      far_fastload->store (__FAR_FastLoads = true);
    }

    // Force VSYNC back on in case of a crash while fast loading
    if (__FAR_FastLoads) {
      far_uncap_fps->set_value (true);
      config.render.framerate.present_interval = 1;
    }

#ifndef WORKING_FPS_UNCAP
    // FORCE OFF UNTIL I CAN FIX
    far_uncap_fps->set_value (false);
#endif


    far_rtss_warned =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (L"RTSS Warning Issued"));

    far_rtss_warned->register_to_ini ( far_prefs,
                                         L"FAR.Compatibility",
                                           L"WarnedAboutRTSS" );

    if (! static_cast <sk::iParameter *> (far_rtss_warned)->load ())
    {
      far_rtss_warned->store (false);
    }

    far_osd_disclaimer =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (L"OSD Disclaimer Dismissed"));

    far_osd_disclaimer->register_to_ini ( far_prefs,
                                            L"FAR.OSD",
                                              L"ShowDisclaimer" );

    if (! static_cast <sk::iParameter *> (far_osd_disclaimer)->load ())
    {
      far_osd_disclaimer->store (true);
    }


    far_accepted_license =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (L"Has accepted the license terms"));

    far_accepted_license->register_to_ini ( far_prefs,
                                              L"FAR.System",
                                                L"AcceptedLicense" );

    if (! static_cast <sk::iParameter *> (far_accepted_license)->load ())
    {
      far_accepted_license->store (false);
    }

    else
      config.imgui.show_eula = (! far_accepted_license->get_value ());


    far_bloom_width =
      dynamic_cast <sk::ParameterInt *>
        (far_factory->create_parameter <int> (L"Width of Bloom Post-Process"));

    far_bloom_width->register_to_ini ( far_prefs,
                                         L"FAR.Lighting",
                                           L"BloomWidth" );

    if (! static_cast <sk::iParameter *> (far_bloom_width)->load ())
    {
      far_bloom_width->store (-1);
    }

    far_bloom.width = far_bloom_width->get_value ();

    // Bloom Width must be > 0 or -1, never 0!
    if (far_bloom.width <= 0) {
      far_bloom.width =                     -1;
      far_bloom_width->store (far_bloom.width);
    }


    far_bloom_disable =
      dynamic_cast <sk::ParameterBool *>
        (far_factory->create_parameter <bool> (L"Disable Bloom"));

    far_bloom_disable->register_to_ini ( far_prefs,
                                           L"FAR.Lighting",
                                             L"DisableBloom" );

    if (! static_cast <sk::iParameter *> (far_bloom_disable)->load ())
    {
      far_bloom_disable->store (false);
    }

    far_bloom.disable = far_bloom_disable->get_value ();


    far_bloom_skip =
      dynamic_cast <sk::ParameterInt *>
        (far_factory->create_parameter <int> (L"Test Texture Skip Factor"));

    far_bloom_skip->register_to_ini ( far_prefs,
                                        L"FAR.Temporary",
                                          L"BloomSkipLevels" );

    if (! static_cast <sk::iParameter *> (far_bloom_skip)->load ())
    {
      far_bloom_skip->store (0);
    }

    far_bloom.skip = far_bloom_skip->get_value ();


    far_fix_motion_blur =
      dynamic_cast <sk::ParameterBool *>
        (far_factory->create_parameter <bool> (L"Test Fix for Motion Blur"));

    far_fix_motion_blur->register_to_ini ( far_prefs,
                                             L"FAR.Temporary",
                                               L"FixMotionBlur" );

    if (! static_cast <sk::iParameter *> (far_fix_motion_blur)->load ())
    {
      far_fix_motion_blur->store (true);
    }

    far_ao.fix_motion_blur = far_fix_motion_blur->get_value ();


    far_ao_disable =
      dynamic_cast <sk::ParameterBool *>
        (far_factory->create_parameter <bool> (L"Disable AO"));

    far_ao_disable->register_to_ini ( far_prefs,
                                        L"FAR.Lighting",
                                          L"DisableAO" );

    if (! static_cast <sk::iParameter *> (far_ao_disable)->load ())
    {
      far_ao_disable->store (false);
    }

    far_ao.disable = far_ao_disable->get_value ();


    far_ao_width =
      dynamic_cast <sk::ParameterInt *>
        (far_factory->create_parameter <int> (L"Width of AO Post-Process"));

    far_ao_width->register_to_ini ( far_prefs,
                                      L"FAR.Lighting",
                                        L"AOWidth" );

    if (! static_cast <sk::iParameter *> (far_ao_width)->load ())
    {
      far_ao_width->store (-1);
    }

    far_ao.width = far_ao_width->get_value ();

    // AO Width must be > 0 or -1, never 0!
    if (far_ao.width <= 0) {
      far_ao.width =                  -1;
      far_ao_width->store (far_ao.width);
    }

    far_ao_height =
      dynamic_cast <sk::ParameterInt *>
        (far_factory->create_parameter <int> (L"Height of AO Post-Process"));

    far_ao_height->register_to_ini ( far_prefs,
                                       L"FAR.Lighting",
                                         L"AOHeight" );

    if (! static_cast <sk::iParameter *> (far_ao_height)->load ())
    {
      far_ao_height->store (-1);
    }

    far_ao.height = far_ao_height->get_value ();

    // AO Height must be > 0 or -1, never 0!
    if (far_ao.height <= 0) {
      far_ao.height =                   -1;
      far_ao_height->store (far_ao.height);
    }



    if (far_lod_patch_s::init ())
    {
      far_lod_fix =
        dynamic_cast <sk::ParameterBool *>
          (far_factory->create_parameter <bool> (L"Disable LOD"));

      far_lod_fix->register_to_ini ( far_prefs,
                                       L"FAR.LOD",
                                         L"ApplyPatch" );

      bool               patch = false;
      far_lod_fix->load (patch);

      far_lod_patch_s::_ApplyPatch = patch;
    }



    auto LoadKeybind =
      [](SK_Keybind* binding, const wchar_t* ini_name) ->
        auto
        {
          auto* ret =
           dynamic_cast <sk::ParameterStringW *>
            (far_factory->create_parameter <std::wstring> (L"DESCRIPTION HERE"));

          ret->register_to_ini ( far_prefs, L"FAR.Keybinds", ini_name );

          if (! static_cast <sk::iParameter *> (ret)->load ())
          {
            binding->parse ();
            ret->store     (binding->human_readable);
          }

          binding->human_readable = ret->get_value ();
          binding->parse ();

          return ret;
        };

    far_hudless_binding = LoadKeybind (&__FAR_HUDLESS.keybind,    L"HUDFreeScreenshot");
    far_center_lock     = LoadKeybind (&far_cam.center_binding,   L"ToggleCameraCenterLock");
    far_focus_lock      = LoadKeybind (&far_cam.focus_binding,    L"ToggleCameraFocusLock");
    far_free_look       = LoadKeybind (&far_cam.freelook_binding, L"ToggleCameraFreelook");



    SK_CreateFuncHook (      L"SK_BeginBufferSwapEx",
                               SK_BeginBufferSwapEx,
                           SK_FAR_EndFrameEx,
      static_cast_p2p <void> (&SK_EndFrameEx_Original) );
    MH_QueueEnableHook (       SK_BeginBufferSwapEx);


    far_prefs->write (far_prefs_file);


    SK_ApplyQueuedHooks ();

    SK_GetCommandProcessor ()->AddVariable ("FAR.GIWorkgroups", SK_CreateVar (SK_IVariable::Int,     &__FAR_GlobalIllumWorkGroupSize));
    //SK_GetCommandProcessor ()->AddVariable ("FAR.BusyWait",     SK_CreateVar (SK_IVariable::Boolean, &__FAR_BusyWait));

    if (game_state.isMicrosoft () || game_state.isSteam ())
    {
      __SK_D3D11_PixelShader_CBuffer_Overrides->push_back (
        { 0x49a3eacf, 48, false, 1, 0, 48, { 0.0f, 1.0f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f } }
      );

      SK_FAR_CB_Override = &__SK_D3D11_PixelShader_CBuffer_Overrides->back ();
    }
  }
}

// Not currently used
bool
WINAPI
SK_FAR_ShutdownPlugin (const wchar_t* backend)
{
  UNREFERENCED_PARAMETER (backend);

  return true;
}

bool
__stdcall
SK_FAR_PlugInCfg (void)
{
  // Push this to FAR.ini so that mod updates don't repeatedly present the user with a license agreement.
  if ((! config.imgui.show_eula) && (! far_accepted_license->get_value ()))
  {
    far_accepted_license->store (true);
    far_prefs->write            (far_prefs_file);
  }


  ImGui::PushID ((const char *)u8"NieR:Automata™_CPL");

  bool changed = false;

  if (ImGui::CollapsingHeader ((const char *)u8"NieR:Automata™", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor (ImGuiCol_Header,        ImVec4 (0.90f, 0.40f, 0.40f, 0.45f));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImVec4 (0.90f, 0.45f, 0.45f, 0.80f));
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImVec4 (0.87f, 0.53f, 0.53f, 0.80f));
    ImGui::TreePush       ("");

    if ((! game_state.isSteam2021 ()) && ImGui::CollapsingHeader ("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
    {
#if 0
      if (game_state.patchable)
      {
        changed |= ImGui::Checkbox ("Manual Override", &SK_FAR_CB_Override->Enable);
      }

      if (SK_FAR_CB_Override->Enable)
      {
        ImGui::TreePush    ("");
        ImGui::BeginGroup  ();
        changed |= ImGui::SliderFloat ("Exposure",   &SK_FAR_CB_Override->Values [1], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat ("Brightness", &SK_FAR_CB_Override->Values [4], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat ("Vignette0",  &SK_FAR_CB_Override->Values [8], 0.0f, 1.0f);
        changed |= ImGui::SliderFloat ("Vignette1",  &SK_FAR_CB_Override->Values [9], 0.0f, 1.0f);
        ImGui::EndGroup    ();
        ImGui::TreePop     ();
      }
#endif

      ImGui::TreePush ("");

      bool bloom = (! far_bloom.disable);

      if (ImGui::Checkbox ("Bloom", &bloom))
      {
        far_bloom.disable = (! bloom);
        far_bloom_disable->store (far_bloom.disable);

        changed = true;
      }

      if (ImGui::IsItemHovered ())
        ImGui::SetTooltip ("For Debug Purposes ONLY, please leave enabled ;)");


      if (! far_bloom.disable)
      {
        ImGui::TreePush ("");

        int bloom_behavior = (far_bloom_width->get_value () != -1) ? 1 : 0;

        ImGui::BeginGroup ();

        if (ImGui::RadioButton ( game_state.isSteam ()
                                   ? "Default Bloom Res. (800x450)"
                                   : "Default Bloom Res. (960x540)", &bloom_behavior, 0))
        {
          changed = true;

          far_bloom_width->store (-1);
        }

        ImGui::SameLine ();

        // 1/4 resolution actually, but this is easier to describe to the end-user
        if (ImGui::RadioButton ("Native Bloom Res.",            &bloom_behavior, 1))
        {
          far_bloom_width->store (static_cast <int> (ImGui::GetIO ().DisplaySize.x));

          changed = true;
        }

        if (ImGui::IsItemHovered ()) {
          ImGui::BeginTooltip ();
          ImGui::Text        ("Improve Bloom Quality");
          ImGui::Separator   ();
          ImGui::BulletText  ("Performance Cost is Negligible");
          ImGui::BulletText  ("Changing this setting requires a full application restart");
          ImGui::EndTooltip  ();
        }

        ImGui::EndGroup ();
        ImGui::TreePop  ();
      }


      bool ao = (! far_ao.disable);

      if (game_state.isSteam ())
      {
        if (ImGui::Checkbox ("Ambient Occlusion", &ao))
        {
          far_ao.disable = (! ao);

          far_ao_disable->store (far_ao.disable);

          changed = true;
        }

        if (ImGui::IsItemHovered ())
          ImGui::SetTooltip ("For Debug Purposes ONLY, please leave enabled ;)");


        if (! far_ao.disable)
        {
          ImGui::TreePush ("");

          int ao_behavior = (far_ao_width->get_value () != -1) ? 3 : 2;

          ImGui::BeginGroup      ();
          if (ImGui::RadioButton ( game_state.isSteam ()
                                  ? "Default AO Res.    (800x450)"
                                  : "Default AO Res.    (960x540)", &ao_behavior, 2))
          {
            changed = true;

            far_ao_width->store  (-1);
            far_ao_height->store (-1);
          }

          ImGui::SameLine ();

          // 1/4 resolution actually, but this is easier to describe to the end-user
          if (ImGui::RadioButton ("Native AO Res.   ",            &ao_behavior, 3))
          {
            far_ao_width->store  (static_cast <int> (ImGui::GetIO ().DisplaySize.x));
            far_ao_height->store (static_cast <int> (ImGui::GetIO ().DisplaySize.y));

            changed = true;
          }

          if (ImGui::IsItemHovered ()) {
            ImGui::BeginTooltip ();
            ImGui::Text        ("Improve AO Quality");
            ImGui::Separator   ();
            ImGui::BulletText  ("Performance Cost is Negligible");
            ImGui::BulletText  ("Changing this setting requires a full application restart");
            ImGui::EndTooltip  ();
          }

          ImGui::EndGroup ();
          ImGui::TreePop  ();
        }
      }

      ImGui::TreePop  ();
    }

    if (ImGui::CollapsingHeader ("Lighting", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::TreePush ("");

      int quality = 0;

      if (__FAR_GlobalIllumWorkGroupSize < 16)
        quality = 0;
      else if (__FAR_GlobalIllumWorkGroupSize < 32)
        quality = 1;
      else if (__FAR_GlobalIllumWorkGroupSize < 64)
        quality = 2;
      else if (__FAR_GlobalIllumWorkGroupSize < 128)
        quality = 3;
      else
        quality = 4;

      if ( ImGui::Combo ( "Global Illumination Quality", &quality, "Off (High Performance)\0"
                                                                   "Low\0"
                                                                   "Medium\0"
                                                                   "High\0"
                                                                   "Ultra (Game Default)\0\0", 5 ) )
      {
        changed = true;

        switch (quality)
        {
          case 0:
            __FAR_GlobalIllumWorkGroupSize = 0;
            break;

          case 1:
            __FAR_GlobalIllumWorkGroupSize = 16;
            break;

          case 2:
            __FAR_GlobalIllumWorkGroupSize = 32;
            break;

          case 3:
            __FAR_GlobalIllumWorkGroupSize = 64;
            break;

          default:
          case 4:
            __FAR_GlobalIllumWorkGroupSize = 128;
            break;
        }
      }

      far_gi_workgroups->store (__FAR_GlobalIllumWorkGroupSize);

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip ();
        ImGui::Text         ("Global Illumination Simulates Indirect Light Bouncing");
        ImGui::Separator    ();
        ImGui::BulletText   ("Lower quality for better performance, but less realistic lighting in shadows.");
        ImGui::BulletText   ("Please direct thanks for this feature to DrDaxxy ;)");
        ImGui::EndTooltip   ();
      }

      if (__FAR_GlobalIllumWorkGroupSize > 64)
      {
        ImGui::SameLine ();
        ImGui::TextColored (ImVec4 (0.5f, 1.0f, 0.1f, 1.0f), " Adjust this for Performance Boost");
        //ImGui::Checkbox ("Compatibility Mode", &__FAR_GlobalIllumCompatMode);
      }

      if (game_state.isSteam () || game_state.isSteam2021 ())
      {
        float extent = __FAR_MINIMUM_EXT * 100.0f;

        if (ImGui::SliderFloat ("Minimum Light Extent", &extent, 0.0f, 100.0f, "%0.2f%%"))
        {
          __FAR_MINIMUM_EXT = std::min (1.0f, std::max (0.0f, extent / 100.0f));

          far_gi_min_light_extent->store (__FAR_MINIMUM_EXT);
        }

        if (ImGui::IsItemHovered ())
        {
          ImGui::BeginTooltip ();
          ImGui::Text         ("Fine-tune Light Culling");
          ImGui::Separator    ();
          ImGui::BulletText   ("Higher values are faster, but will produce visible artifacts.");
          ImGui::BulletText   ("Use Park Ruins: Attraction Sq. as a reference when adjusting this.");
          ImGui::EndTooltip   ();
        }
      }

      ImGui::TreePop ();
    }

    if (game_state.patchable && ImGui::CollapsingHeader ("Framerate"))
    {
      ImGui::TreePush ("");

      bool remove_cap = far_uncap_fps->get_value ();

#ifdef WORKING_FPS_UNCAP
      if (ImGui::Checkbox ("Remove 60 FPS Cap  ", &remove_cap))
      {
        changed = true;

        SK_FAR_SetFramerateCap (remove_cap);
        far_uncap_fps->store   (remove_cap);

        if (! remove_cap)
        {
                               __FAR_FastLoads = false;
          far_fastload->store (__FAR_FastLoads);
        }
      }

      if (ImGui::IsItemHovered ())
      {
        ImGui::BeginTooltip  ();
        ImGui::Text          ("Can be toggled with "); ImGui::SameLine ();
        ImGui::TextColored   (ImVec4 (1.0f, 0.8f, 0.1f, 1.0f), "Ctrl + Shift + .");
        ImGui::Separator     ();
        ImGui::TreePush      ("");

        if (game_state.isSteam ())
        {
          ImGui::TextColored (ImVec4 (0.9f, 0.9f, 0.9f, 1.0f), "Two things to consider when enabling this");
          ImGui::TreePush    ("");
          ImGui::BulletText  ("The game has no refresh rate setting, edit dxgi.ini to establish fullscreen refresh rate.");
          ImGui::BulletText  ("The mod is pre-configured with a 59.94 FPS framerate limit, adjust accordingly.");
          ImGui::TreePop     ();
        }

        else
        {
          ImGui::BulletText  ("Menus must be capped to 60.0 FPS, but most gameplay supports arbitrary framerate");
        }

        ImGui::TreePop       ();
        ImGui::EndTooltip    ();
      }

      if (remove_cap && (! game_state.isSteam ()))
      {
        ImGui::SameLine ();

        if (ImGui::Checkbox ("Fast Load Mode", &__FAR_FastLoads))
        {
          changed = true;

          far_fastload->store (__FAR_FastLoads);

          if (__FAR_FastLoads)
          {
            SK_FAR_SetFramerateCap (true);
            far_uncap_fps->store   (true);
          }
        }

        if (ImGui::IsItemHovered ())
        {
          ImGui::SetTooltip ("Disables framerate limits during load screens to speed things the hell up.");
        }
      }
#endif

      ImGui::TreePop ();
    }

    if (game_state.isSteam2021 () && ImGui::CollapsingHeader ("Level of Detail"))
    {
      ImGui::TreePush ("");

      if (far_lod_patch_s::IsAOAllowed_Orig != nullptr)
      {
        bool patch =
          far_lod_patch_s::_ApplyPatch;

        if (ImGui::Checkbox ("Enable LOD Patch", &patch))
        {
          changed = true;

          far_lod_fix->store (patch);

          far_lod_patch_s::_ApplyPatch = patch;
        }
      }

      ImGui::TreePop ();
    }

    if ((game_state.isSteam () /*|| game_state.isSteam2021 ()*/) && ImGui::CollapsingHeader ("Camera and HUD"))
    {
      auto Keybinding = [](SK_Keybind* binding, sk::ParameterStringW* param) ->
        auto
        {
          std::string label  = SK_WideCharToUTF8 (binding->human_readable) + "##";
                      label += binding->bind_name;

          if (ImGui::Selectable (label.c_str (), false))
          {
            ImGui::OpenPopup (binding->bind_name);
          }

          std::wstring   original_binding =
                                  binding->human_readable;
          SK_ImGui_KeybindDialog (binding);

          if (original_binding != binding->human_readable)
          {         param->store (binding->human_readable);

            return true;
          }

          return false;
        };

      ImGui::TreePush      ("");

      if (                                 game_state.pHUDOpacity != nullptr)
      ImGui::SliderFloat   ("HUD Opacity", game_state.pHUDOpacity, 0.0f, 2.0f);

      ImGui::Text          ("HUD Free Screenshot Keybinding:  "); ImGui::SameLine ();

      changed |= Keybinding (&__FAR_HUDLESS.keybind, far_hudless_binding);

      ImGui::Separator ();

      if (ImGui::Checkbox   ("Lock Camera Origin", &far_cam.center_lock))
      {
        far_cam.center_lock = (! far_cam.center_lock);
        far_cam.toggleCenterLock ();
      }

      ImGui::SameLine       ();
      changed |= Keybinding (&far_cam.center_binding, far_center_lock);

      if (ImGui::Checkbox   ("Lock Camera Focus", &far_cam.focus_lock))
      {
        far_cam.focus_lock = (! far_cam.focus_lock);
        far_cam.toggleFocusLock ();
      }


      ImGui::SameLine ();
      changed |= Keybinding (&far_cam.focus_binding, far_focus_lock);

      ImGui::Checkbox ("Use Gamepad Freelook", &__FAR_Freelook);

      ImGui::SameLine       ();
      changed |= Keybinding (&far_cam.freelook_binding, far_free_look);

      ImGui::Separator ();

      ImGui::Text ( "Origin: (%.3f, %.3f, %.3f) - Look: (%.3f,%.3f,%.3f",
                      ((float *)far_cam.pCamera)[0], ((float *)far_cam.pCamera)[1], ((float *)far_cam.pCamera)[2],
                      ((float *)far_cam.pLook)[0],   ((float *)far_cam.pLook)[1],   ((float *)far_cam.pLook)[2] );

      ImGui::TreePop        ();
    }

    bool reshade =
      ImGui::CollapsingHeader ("ReShade", ImGuiTreeNodeFlags_DefaultOpen);

    if (ImGui::IsItemHovered ())
      ImGui::SetTooltip ("Use File | Browse ReShade Assets to manage shaders and textures.");

    if (reshade)
    {
      static wchar_t
           wszReShadePath [MAX_PATH + 2] = { };
      if (*wszReShadePath == L'\0')
      {
        if (game_state.isSteam () || game_state.isSteam2021 ())
             SK_PathCombineW (wszReShadePath, SK_GetHostPath   (), L"ReShade64.dll");
        else SK_PathCombineW (wszReShadePath, SK_GetConfigPath (), L"ReShade64.dll");
        // The Microsoft Store version runs from a read-only directory.
      }

      ImGui::TreePush      ("");
      static SK_Import_LoadOrder
                  load_order     = SK_Import_LoadOrder::Early;
             bool enabled        =
        SK_GetDLLConfig ()->contains_section (L"Import.ReShade64_Custom");
             bool plugin_changed =
        SK_ImGui_PlugInSelector ( SK_GetDLLConfig (),
                                    "ReShade (Custom)",
                                      wszReShadePath,
                                        L"Import.ReShade64_Custom",
                                          enabled,
                                            load_order,
                                              SK_Import_LoadOrder::PlugIn );

      if (enabled)
        SK_ImGui_PlugInDisclaimer ();

      if (plugin_changed)
      {
        SK_ImGui_SavePlugInPreference (
          SK_GetDLLConfig (),
            enabled,
              L"Import.ReShade64_Custom",
                L"Unofficial",
                  load_order,
                    wszReShadePath,
                      L"Normal"
        );
      }
      ImGui::TreePop       (  );
    }

    ImGui::TreePop       ( );
    ImGui::PopStyleColor (3);
  }

  if (changed)
    far_prefs->write (far_prefs_file);

  ImGui::PopID ();

  return true;
}

bool
__stdcall
SK_FAR_IsPlugIn (void)
{
  return far_prefs != nullptr;
}

void
far_game_state_s::uncapFPS (void)
{
  DWORD old_protect_mask;

  SK_RunOnce (SK_FAR_SetLimiterWait (SK_FAR_WaitBehavior::Busy));

  __target_fps =
    config.render.framerate.target_fps;

  if (! game_state.isSteam2021 ())
  {
    mbegin (pspinlock, 2)
    memset (pspinlock, 0x90, 2);
    mend   (pspinlock, 2)
  }

  else
  {
    mbegin (pspinlock, 2)
    pspinlock [0] = 0xEB;
    pspinlock [1] = 0x48;
    mend   (pspinlock, 2)
  }

  if (game_state.isSteam ())
  {
    mbegin (pmin_tstep, 1)
    *pmin_tstep = 0xEB;
    mend   (pmin_tstep, 1)

    mbegin (pmax_tstep, 2)
    pmax_tstep [0] = 0x90;
    pmax_tstep [1] = 0xE9;
    mend   (pmax_tstep, 2)
  }
}



void
far_game_state_s::capFPS (void)
{
  SK_RunOnce (SK_FAR_SetLimiterWait (SK_FAR_WaitBehavior::Busy));

  //if (! far_limiter_busy->get_value ())
  //  SK_FAR_SetLimiterWait (SK_FAR_WaitBehavior::Sleep);
  //else {
  // Save and later restore FPS
  //
  //   Avoid using Special K's command processor because that
  //     would store this value persistently.
  if ( config.render.framerate.target_fps <= 0.0f ||
       config.render.framerate.target_fps > 60.0f ||
                             __target_fps > 60.0f ||
                             __target_fps <= 0.0f )
  {
                             __target_fps = 60.0f;
  }

  if (game_state.isSteam2021 ())
    return;

  DWORD  old_protect_mask;

  if (game_state.isSteam ())
  {
    mbegin (pspinlock, 2)
    pspinlock [0] = 0x77;
    pspinlock [1] = 0x9F;
    mend   (pspinlock, 2)

    mbegin (pmin_tstep, 1)
    *pmin_tstep = 0x73;
    mend   (pmin_tstep, 1)

    mbegin (pmax_tstep, 2)
    pmax_tstep [0] = 0x0F;
    pmax_tstep [1] = 0x86;
    mend   (pmax_tstep, 2)
  }

  else
  {
    if (! game_state.isSteam2021 ())
    {
      mbegin (pspinlock, 2)
      pspinlock [0] = 0x77;
      pspinlock [1] = 0x9D;
      mend   (pspinlock, 2)
    }

    else
    {
      mbegin (pspinlock, 2)
      pspinlock [0] = 0x7E;
      pspinlock [1] = 0x48;
      mend   (pspinlock, 2)
    }
  }
}


#undef mbegin
#undef mend






void far_game_state_s::initForSteam2021 (void)
{
  bSteam          = false;
  bSteam2021      = true;
  __FAR_FastLoads = true;

  uiBaseAddr =
    reinterpret_cast <uintptr_t> (
      SK_Debug_GetImageBaseAddr ()
    );

  // NieRAutomata.exe+278FE9   -    mov [NieRAutomata.exe+0F82E38],ebx      ; Mouse Cursor Vis

  // NieRAutomata.exe+8BEB8D - C7 83 E4010000 01000000 - mov [rbx+000001E4],00000001 { 1 }  ; Enter Menu
  // NieRAutomata.exe+8CC7BA - 89 73 4C                - mov [rbx+4C],esi                   ; Leave Menu
  pMenu      = reinterpret_cast <DWORD *> (uiBaseAddr + 0x0F68CD4);
                                                      //  ^^^ Menu (whether gamepad or mouse)

  // NieRAutomata.exe+35CD68   -    mov [NieRAutomata.exe+100D410],00000001 ; Load Start
  // NieRAutomata.exe+3F3056   -    mov [NieRAutomata.exe+100D410],ebx      ; Load End
  pLoading   = reinterpret_cast <DWORD *> (uiBaseAddr + 0x100D410);
  pLoading2  = reinterpret_cast <DWORD *> (uiBaseAddr + 0x1251180);

  // Cutscne
  pHacking   = nullptr;

  // Shortcut
  //NieRAutomata.exe+7521E9 - E8 92570A00           - call NieRAutomata.exe+7F7980
  //NieRAutomata.exe+7521EE - 89 05 6C758D00        - mov [NieRAutomata.exe+1029760],eax { (0) }
  pShortcuts = reinterpret_cast <DWORD *> (uiBaseAddr + 0x1029760);

  far_cam.pCamera = reinterpret_cast <vec3_t *> (uiBaseAddr + 0x1020960);
  far_cam.pLook   = reinterpret_cast <vec3_t *> (uiBaseAddr + 0x1020970);
  far_cam.pRoll   = reinterpret_cast <float  *> (uiBaseAddr + 0x10209A8);
}