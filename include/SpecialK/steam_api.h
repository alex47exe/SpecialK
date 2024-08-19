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
#ifndef __SK__STEAM_API_H__
#define __SK__STEAM_API_H__

struct IUnknown;
#include <Unknwnbase.h>

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include <steamapi/steam_api.h>
#include <steamapi/isteaminput.h>
#include <SpecialK/log.h>
#include <SpecialK/command.h>
#include <SpecialK/render/screenshot.h>
#include <SpecialK/storefront/achievements.h>


#define STEAM_CALLRESULT( thisclass, func, param, var ) CCallResult< thisclass, param > var; void func( param *pParam, bool )

typedef uint64_t AppId64_t;

//
// SK has an official Steam AppID, and it is deliberately configured to
//   disable many annoying Steam client "features" (e.g. Steam Input) ;)
// 
//   * Money well spent!
//
constexpr AppId_t SPECIAL_KILLER_APPID = 1157970;

namespace SK
{
  namespace SteamAPI
  {
    void Init     (bool preload);
    void Shutdown (void);
    void Pump     (void);


    void  __stdcall SetOverlayState  (bool active);
    bool  __stdcall GetOverlayState  (bool real);
    bool  __stdcall IsOverlayAware   (void); // Did the game install a callback?

    void  __stdcall UpdateNumPlayers (void);
    int   __stdcall GetNumPlayers    (void);

    float __stdcall PercentOfAchievementsUnlocked (void);
    int   __stdcall NumberOfAchievementsUnlocked  (void);

    bool  __stdcall TakeScreenshot   (SK_ScreenshotStage when = SK_ScreenshotStage::EndOfFrame, bool allow_sound = true, std::string title = "");


    AppId64_t    AppID               (void);
    std::string& AppName             (void);

    CSteamID     UserSteamID         (void);

    LONGLONG     GetCallbacksRun     (void);

    // Override Steam client's tracking of input focus (for current AppID)
    bool         SetWindowFocusState (bool focused);


    // The state that we are explicitly telling the game
    //   about, not the state of the actual overlay...
    extern bool      overlay_state;

    extern uint64_t  steam_size;
    // Must be global for x86 ABI problems
    extern CSteamID  player;


    enum ELogonState
    {
      k_ELogonStateNotLoggedOn = 0,
      k_ELogonStateLoggingOn   = 1,
      k_ELogonStateLoggingOff  = 2,
      k_ELogonStateLoggedOn    = 3
    };

    // Exposes a few operations not found in modern SteamAPI interfaces,
    //   some of which are needed to stop the Steam client from doing crazy
    //     things during debug sessions.
    //
    //   * We can get these directly from steamclient{64}.dll, but this
    //       is actually a hell of a lot more convenient and presumably
    //         less likely to interact in unexpected ways in the future.
    //
    class ISteamUser004_Light
    {
    public:
      // returns the HSteamUser this interface represents
      virtual HSteamUser  GetHSteamUser (void)             = 0;

      // steam account management functions
      virtual void        LogOn         (CSteamID steamID) = 0;
      virtual void        LogOff        (void)             = 0;
      virtual bool        BLoggedOn     (void)             = 0;
      virtual ELogonState GetLogonState (void)             = 0;
      virtual bool        BConnected    (void)             = 0;
      virtual CSteamID    GetSteamID    (void)             = 0;

      // The vftable is quite a bit larger than this, but it's all supposedly
      //   obsolete and of no interest. This is the minimal ABI for our purposes.
    };

    void          SetPersonaState (EPersonaState state);
    EPersonaState GetPersonaState (void);

    std::string   GetConfigDir (void);
    std::string   GetDataDir   (void);
  }
}



// Tests the Import Table of hMod for anything Steam-Related
//
//   If found, and this test is performed after the pre-init
//     DLL phase, SteamAPI in one of its many forms will be
//       hooked.
bool
SK_Steam_TestImports (HMODULE hMod);

void SK_InitSteamTier0 (void);

void
SK_Steam_InitCommandConsoleVariables (void);

class SK_Steam_ScreenshotManager
{
public:
  enum class ScreenshotStatus
  {
    Success = 0,
    Fail    = 1,

    _Types
  };

  static constexpr UINT _StatusTypes =
    static_cast <UINT> (ScreenshotStatus::_Types);

  SK_Steam_ScreenshotManager (void) noexcept :
       request ( this, &SK_Steam_ScreenshotManager::OnScreenshotRequest ),
       ready   ( this, &SK_Steam_ScreenshotManager::OnScreenshotReady )
  {
    init ();
  }

  ~SK_Steam_ScreenshotManager (void);


  STEAM_CALLBACK ( SK_Steam_ScreenshotManager,
                   OnScreenshotRequest,
                   ScreenshotRequested_t,
                   request );

  STEAM_CALLBACK ( SK_Steam_ScreenshotManager,
                   OnScreenshotReady,
                   ScreenshotReady_t,
                   ready );

  ScreenshotStatus
  WaitOnScreenshot (ScreenshotHandle handle, DWORD dwTimeoutMs = 2500UL);

  void init (void) noexcept;

protected:
  concurrency::concurrent_unordered_map <ScreenshotHandle, EResult>
                          screenshots_handled;
  HANDLE                  hSigReady [_StatusTypes] = {
    INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE
  };

private:
} extern *steam_screenshot_manager;

size_t SK_SteamAPI_GetNumPossibleAchievements (void);

std::vector <SK_Achievement *>& SK_SteamAPI_GetUnlockedAchievements (void);
std::vector <SK_Achievement *>& SK_SteamAPI_GetLockedAchievements   (void);
std::vector <SK_Achievement *>& SK_SteamAPI_GetAllAchievements      (void);

float  SK_SteamAPI_GetUnlockedPercentForFriend      (uint32_t friend_idx);
size_t SK_SteamAPI_GetUnlockedAchievementsForFriend (uint32_t friend_idx, BOOL* pStats);
size_t SK_SteamAPI_GetLockedAchievementsForFriend   (uint32_t friend_idx, BOOL* pStats);
size_t SK_SteamAPI_GetSharedAchievementsForFriend   (uint32_t friend_idx, BOOL* pStats);


// Returns true if all friend stats have been pulled from the server
      bool  __stdcall SK_SteamAPI_FriendStatsFinished  (void);

// Percent (0.0 - 1.0) of friend achievement info fetched
     float  __stdcall SK_SteamAPI_FriendStatPercentage (void);

       int  __stdcall SK_SteamAPI_GetNumFriends        (void);
const std::string& // Do not DLL export this, pass a buffer for storage if export is needed
            __stdcall SK_SteamAPI_GetFriendName        (uint32_t friend_idx, size_t* pLen = nullptr);


bool __stdcall        SK_SteamAPI_TakeScreenshot                (void);
bool __stdcall        SK_IsSteamOverlayActive                   (void);
bool __stdcall        SK_SteamOverlay_GoToURL                   (const char* szURL, bool bUseWindowsShellIfOverlayFails = false);

void    __stdcall     SK_SteamAPI_UpdateNumPlayers              (void);
int32_t __stdcall     SK_SteamAPI_GetNumPlayers                 (void);

float __stdcall       SK_SteamAPI_PercentOfAchievementsUnlocked (void);

void                  SK_SteamAPI_LogAllAchievements            (void);
void                  SK_UnlockSteamAchievement                 (uint32_t idx);

bool                  SK_Steam_Imported                         (void);

void                  SK_HookCSteamworks                        (void);
int                   SK_HookSteamAPI                           (void);

void                  SK_Steam_ClearPopups                      (void);
int                   SK_Steam_DrawOSD                          (void);

void                  SK_Steam_ProcessWindowActivation          (bool active);
void                  SK_Steam_ForceInputAppId                  (uint64_t appid);
void                  SK_SteamInput_Unfux0r                     (void);
bool                  SK_Steam_LoadOverlayEarly                 (void);

void                  SK_Steam_InitCommandConsoleVariables      (void);

ISteamUtils*          SK_SteamAPI_Utils                         (void);
ISteamMusic*          SK_SteamAPI_Music                         (void);
ISteamRemoteStorage*  SK_SteamAPI_RemoteStorage                 (void);
ISteamFriends*        SK_SteamAPI_Friends                       (void);

uint32_t __stdcall    SK_Steam_PiratesAhoy                      (void);





#include <SpecialK/hooks.h>


using SteamAPI_Init_pfn                  = bool (S_CALLTYPE *    )(void);
using SteamAPI_InitSafe_pfn              = bool (S_CALLTYPE *)(void);
using SteamAPI_ManualDispatch_Init_pfn   = void (S_CALLTYPE *)(void);

using SteamAPI_RestartAppIfNecessary_pfn = bool (S_CALLTYPE *)
    (uint32 unOwnAppID);
using SteamAPI_IsSteamRunning_pfn        = bool (S_CALLTYPE *)(void);

using SteamAPI_Shutdown_pfn              = void (S_CALLTYPE *)(void);

using SteamAPI_RegisterCallback_pfn      = void (S_CALLTYPE *)
    (class CCallbackBase *pCallback, int iCallback);
using SteamAPI_UnregisterCallback_pfn    = void (S_CALLTYPE *)
    (class CCallbackBase *pCallback);

using SteamAPI_RegisterCallResult_pfn   = void (S_CALLTYPE *)
    (class CCallbackBase *pCallback, SteamAPICall_t hAPICall );
using SteamAPI_UnregisterCallResult_pfn = void (S_CALLTYPE *)
    (class CCallbackBase *pCallback, SteamAPICall_t hAPICall );

using SteamAPI_RunCallbacks_pfn         = void (S_CALLTYPE *)(void);

using SteamAPI_GetHSteamUser_pfn        = HSteamUser (*)(void);
using SteamAPI_GetHSteamPipe_pfn        = HSteamPipe (*)(void);

using SteamClient_pfn                   = ISteamClient* (S_CALLTYPE *)(void);

//using GetControllerState_pfn            = bool (*)
//    (ISteamController* This, uint32 unControllerIndex, SteamControllerState_t *pState);

using SteamAPI_InitSafe_pfn             = bool (S_CALLTYPE*)(void);
using SteamAPI_Init_pfn                 = bool (S_CALLTYPE*)(void);
using SteamAPI_ManualDispatch_Init_pfn  = void (S_CALLTYPE*)(void);

using SteamAPI_GetSteamInstallPath_pfn  = const char* (S_CALLTYPE *)(void);

using SteamAPI_ISteamInput_Init_pfn     = bool (S_CALLTYPE *)(ISteamInput* This, bool bExplicitlyCallRunFrame);



extern "C" SteamAPI_InitSafe_pfn              SteamAPI_InitSafe_Original             ;
extern "C" SteamAPI_Init_pfn                  SteamAPI_Init_Original                 ;
extern "C" SteamAPI_ManualDispatch_Init_pfn   SteamAPI_ManualDispatch_Init_Original  ;

extern "C" SteamAPI_RunCallbacks_pfn          SteamAPI_RunCallbacks                  ;
extern "C" SteamAPI_RunCallbacks_pfn          SteamAPI_RunCallbacks_Original         ;

extern "C" SteamAPI_RegisterCallback_pfn      SteamAPI_RegisterCallback              ;
extern "C" SteamAPI_RegisterCallback_pfn      SteamAPI_RegisterCallback_Original     ;

extern "C" SteamAPI_UnregisterCallback_pfn    SteamAPI_UnregisterCallback            ;
extern "C" SteamAPI_UnregisterCallback_pfn    SteamAPI_UnregisterCallback_Original   ;

extern "C" SteamAPI_RegisterCallResult_pfn    SteamAPI_RegisterCallResult            ;
extern "C" SteamAPI_UnregisterCallResult_pfn  SteamAPI_UnregisterCallResult          ;

extern "C" SteamAPI_Init_pfn                  SteamAPI_Init                          ;
extern "C" SteamAPI_InitSafe_pfn              SteamAPI_InitSafe                      ;
extern "C" SteamAPI_ManualDispatch_Init_pfn   SteamAPI_ManualDispatch_Init           ;

extern "C" SteamAPI_RestartAppIfNecessary_pfn SteamAPI_RestartAppIfNecessary         ;
extern "C" SteamAPI_IsSteamRunning_pfn        SteamAPI_IsSteamRunning                ;

extern "C" SteamAPI_GetHSteamUser_pfn         SteamAPI_GetHSteamUser                 ;
extern "C" SteamAPI_GetHSteamPipe_pfn         SteamAPI_GetHSteamPipe                 ;

extern "C" SteamClient_pfn                    SteamClient                            ;
extern "C" SteamClient_pfn                    SteamClient_Original                   ;

extern "C" SteamAPI_Shutdown_pfn              SteamAPI_Shutdown                      ;
extern "C" SteamAPI_Shutdown_pfn              SteamAPI_Shutdown_Original             ;

extern "C" SteamAPI_ISteamInput_Init_pfn      SteamAPI_ISteamInput_Init_Original     ;

extern "C" SteamAPI_GetSteamInstallPath_pfn   SteamAPI_GetSteamInstallPath           ;

//extern "C" GetControllerState_pfn             GetControllerState_Original;


extern "C" bool S_CALLTYPE SteamAPI_InitSafe_Detour            (void);
extern "C" bool S_CALLTYPE SteamAPI_Init_Detour                (void);
extern "C" void S_CALLTYPE SteamAPI_ManualDispatch_Init_Detour (void);
extern "C" void S_CALLTYPE SteamAPI_RunCallbacks_Detour        (void);

extern "C" void S_CALLTYPE SteamAPI_RegisterCallback_Detour   (class CCallbackBase *pCallback, int iCallback);
extern "C" void S_CALLTYPE SteamAPI_UnregisterCallback_Detour (class CCallbackBase *pCallback);


void SK_SteamAPI_InitManagers            (void);
void SK_SteamAPI_DestroyManagers         (void);

extern "C" void S_CALLTYPE SteamAPI_Shutdown_Detour (void);
void                       SK_Steam_StartPump       (bool force = false);


ISteamMusic*
SAFE_GetISteamMusic (ISteamClient* pClient, HSteamUser hSteamuser, HSteamPipe hSteamPipe, const char *pchVersion);

class SK_SteamAPIContext : public SK_IVariableListener
{
using ISteamUser004_Light = SK::SteamAPI::ISteamUser004_Light;

public:
  virtual ~SK_SteamAPIContext (void) noexcept { };

  bool OnVarChange (SK_IVariable* var, void* val = nullptr) override;

  bool InitCSteamworks (HMODULE hSteamDLL);
  bool InitSteamAPI    (HMODULE hSteamDLL);

  STEAM_CALLRESULT ( SK_SteamAPIContext,
                     OnFileDetailsDone,
                     FileDetailsResult_t,
                     get_file_details );

  void Shutdown (bool bGameRequested = false);

  ISteamUser*          User                 (void) noexcept { return user_;               }
  int                  UserVersion          (void) noexcept { return user_ver_;           }
  ISteamUser004_Light* UserEx               (void) noexcept { return user_ex_;            }
  ISteamUserStats*     UserStats            (void) noexcept { return user_stats_;         }
  ISteamApps*          Apps                 (void) noexcept { return apps_;               }
  ISteamFriends*       Friends              (void) noexcept { return friends_;            }
  ISteamUtils*         Utils                (void) noexcept { return utils_;              }
  int                  UtilsVersion         (void) noexcept { return utils_ver_;          }
  ISteamScreenshots*   Screenshots          (void) noexcept { return screenshots_;        }
  ISteamController*    Controller           (void) noexcept { return controller_;         }
  ISteamMusic*         Music                (void) noexcept { return music_;              }
  ISteamRemoteStorage* RemoteStorage        (void) noexcept { return remote_storage_;     }
  int                  RemoteStorageVersion (void) noexcept { return remote_storage_ver_; }
  ISteamUGC*           UGC                  (void) noexcept { return ugc_;                }
  int                  UGCVersion           (void) noexcept { return ugc_ver_;            }
  ISteamInput*         Input                (void) noexcept { return input_;              }

  SK_IVariable*      popup_origin   = nullptr;
  SK_IVariable*      notify_corner  = nullptr;

  SK_IVariable*      tbf_pirate_fun = nullptr;
  float              tbf_float      = 45.0F;

  // Backing storage for the human readable variable names,
  //   the actual system uses an integer value but we need
  //     storage for the cvars.
  struct {
    char popup_origin  [16] = { "DontCare" };
    char notify_corner [16] = { "DontCare" };
  } var_strings;

  const char*        GetSteamInstallPath (void);

  void* ClientEngine  (void);
  void* ClientFriends (void);
  void* ClientUser    (void);
  void* ClientUtils   (void);

  AppId64_t  ReassignAppIDForPipe (HSteamPipe hPipe, AppId64_t nAppID, bool bTrackProcess);
  HSteamPipe GetGamePipe          (void) noexcept { return hSteamPipe; }

  bool       SetWindowFocusState  (bool focused);

  // We create extra pipes any time SteamAPI stuff is invoked from a different
  //   thread, but we need to shut these down prior to application exit.
  bool  ReleaseThreadPipe (void);
  bool  ReleaseThreadUser (void);

protected:

private:
  HSteamPipe           hSteamPipe      = 0;
  HSteamUser           hSteamUser      = 0;

  ISteamClient*        client_         = nullptr;
  ISteamUser*          user_           = nullptr;
  ISteamUser004_Light* user_ex_        = nullptr;
  ISteamUserStats*     user_stats_     = nullptr;
  ISteamApps*          apps_           = nullptr;
  ISteamFriends*       friends_        = nullptr;
  ISteamUtils*         utils_          = nullptr;
  ISteamScreenshots*   screenshots_    = nullptr;
  ISteamController*    controller_     = nullptr;
  ISteamMusic*         music_          = nullptr;
  ISteamRemoteStorage* remote_storage_ = nullptr;
  ISteamUGC*           ugc_            = nullptr;
  ISteamInput*         input_          = nullptr;

  int                  client_ver_         = 0;
  int                  ugc_ver_            = 0;
  int                  user_ver_           = 0;
  int                  utils_ver_          = 0;
  int                  remote_storage_ver_ = 0;
  int                  input_ver_          = 0;
};

extern SK_LazyGlobal <SK_SteamAPIContext> pSteamCtx;
#define steam_ctx pSteamCtx.get ()

#include <SpecialK/log.h>

extern volatile LONG             __SK_Steam_init;
extern volatile LONG             __SteamAPI_hook;

extern          CRITICAL_SECTION callback_cs;
extern          CRITICAL_SECTION init_cs;
extern          CRITICAL_SECTION popup_cs;


enum class SK_SteamUser_LoggedOn_e
{
  Unknown  =  -1,
  Offline  = 0x0,
  Online   = 0x1,

  Spoofing = 0x2
};


// Returns the REAL state, masked with any necessary spoofing
SK_SteamUser_LoggedOn_e
SK_SteamUser_BLoggedOn (void);

const wchar_t*
SK_Steam_PopupOriginToWStr (int origin);

int
SK_Steam_PopupOriginWStrToEnum (const wchar_t* str);

BOOL
SK_Steam_PreHookCore (const wchar_t* wszTry = nullptr);

BOOL
SK_Steam_KickStart   (const wchar_t* szLibPath = nullptr);

void
SK_Steam_KillPump (void);

const wchar_t*
SK_Steam_GetDLLPath (void);

void
SK_Steam_SetNotifyCorner (void);


void
__stdcall
SK_SteamAPI_SetOverlayState (bool active);

bool
__stdcall
SK_SteamAPI_GetOverlayState (bool real);


void
SK_Steam_UnlockAchievement (uint32_t idx);

void
SK_Steam_LoadUnlockSound (const wchar_t* wszUnlockSound);


// Returns the size of all redistributable files wasting disk space across all Steam Libraries
//
//   May optionally be used to erase the files since they are not needed after a game is installed.
uint64_t
SK_Steam_ScrubRedistributables (int& total_files, bool erase = false);


// Certain Steam Overlay functionality requires a connection;
//
//   This function bypasses SteamAPI and can establish a Steam client connection
bool
SK_Steam_ConnectUserIfNeeded (CSteamID user);

std::wstring
SK_Steam_GetApplicationManifestPath (AppId64_t appid = 0);

uint64_t
SK_Steam_GetAppID_NoAPI (void);

std::string
SK_UseManifestToGetAppName (AppId64_t appid = 0);

struct SK_Steam_Depot
{
  std::string  name; // Has to be queried w/ WebAPI
  DepotId_t    depot;
  ManifestId_t manifest;
};

std::vector <SK_Steam_Depot>
SK_UseManifestToGetDepots (AppId_t appid = 0);

ManifestId_t
SK_UseManifestToGetDepotManifest (AppId_t appid, DepotId_t depot);



#include <stack>

// Barely functional Steam Key/Value Parser
//   -> Does not handle unquoted kv pairs.
class SK_Steam_KeyValues
{
public:
  static
  std::vector <std::string>
  getKeys ( const std::string                &input,
            const std::deque  <std::string>  &sections,
                  std::vector <std::string>* values = nullptr )
  {
    std::vector <std::string> ret;

    if (sections.empty () || input.empty ())
      return ret;

    struct {
      std::deque <std::string> path;

      struct {
        std::string actual;
        std::string test;
      } heap;

      void heapify (std::deque <std::string> const* sections = nullptr)
      {
        int i = 0;

        auto& in  = (sections == nullptr) ? path        : *sections;
        auto& out = (sections == nullptr) ? heap.actual : heap.test;

        out = "";

        for ( auto& str : in )
        {
          if (i++ > 0)
            out += "\x01";

            out += str;
        }
      }
    } search_tree;

    search_tree.heapify (&sections);

    std::string name   = "";
    std::string value  = "";
    int         quotes = 0;

    const auto clear =
   [&](void) noexcept
    {
      name.clear  ();
      value.clear ();
      quotes = 0;
    };

    for (auto c : input)
    {
      if (c == '"')
        ++quotes;

      else if (c != '{')
      {
        if (quotes == 1)
        {
          name += c;
        }

        if (quotes == 3)
        {
          value += c;
        }
      }

      if (quotes == 4)
      {
        if (0 == _stricmp ( search_tree.heap.test.c_str   (),
                            search_tree.heap.actual.c_str () ) )
        {
          ret.emplace_back (name);

          if (values != nullptr)
            values->emplace_back (value);
        }

        clear ();
      }

      if (c == '{')
      {
        search_tree.path.push_back (name);
        search_tree.heapify        (    );

        clear ();
      }

      else if (c == '}')
      {
        search_tree.path.pop_back ();

        clear ();
      }
    }

    return ret;
  }

  static
  std::string
  getValue ( const std::string              &input,
             const std::deque <std::string> &sections,
             const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    size_t idx = 0;
    for ( auto& it : keys )
    {
      if (it._Equal (key))
        return values [idx];

      ++idx;
    }

    return "";
  }

  static
  std::wstring
  getValueAsUTF16 ( const std::string              &input,
                    const std::deque <std::string> &sections,
                    const std::string              &key )
  {
    std::vector <std::string> values;
    std::vector <std::string> keys (
      SK_Steam_KeyValues::getKeys (input, sections, &values)
    );

    int idx = 0;

    for ( auto& it : keys )
    {
      if (it._Equal (key))
      {
        return
          SK_UTF8ToWideChar (
            values [idx]
          );
      }

      ++idx;
    }

    return L"";
  }
};



ScreenshotHandle
WINAPI
SK_SteamAPI_AddScreenshotToLibraryEx ( const char *pchFilename,
                                       const char *pchThumbnailFilename,
                                             int   nWidth,
                                             int   nHeight,
                                             bool  Wait = false );


extern volatile LONG SK_SteamAPI_CallbackRateLimit;

extern SK_Thread_HybridSpinlock* steam_callback_cs;
extern SK_Thread_HybridSpinlock* platform_popup_cs;
extern SK_Thread_HybridSpinlock* steam_init_cs;

extern void SK_Platform_GetUserName (char *pszName, int max_len);

extern SK_LazyGlobal <Concurrency::concurrent_unordered_map <DepotId_t, SK_DepotList> >           SK_Steam_DepotManifestRegistry;
extern SK_LazyGlobal <Concurrency::concurrent_unordered_map <DepotId_t, SK_Steam_DepotManifest> > SK_Steam_InstalledManifest;

#endif /* __SK__STEAM_API_H__ */