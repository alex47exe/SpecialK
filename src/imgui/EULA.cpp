/**
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


std::string
SK_GetLicenseText (SHORT id)
{
  HMODULE hMod = SK_GetDLL ();
  HRSRC   res;

  // NOTE: providing g_hInstance is important, NULL might not work
  res =
    FindResource ( hMod, MAKEINTRESOURCE (id), L"TEXT" );

  if (res)
  {
    DWORD   res_size    = SizeofResource ( hMod, res );
    HGLOBAL license_ref = LoadResource   ( hMod, res );

    // There is no foreseeable reason this would be NULL, but the Application Verifier will not shut up about it.
    if (! license_ref) return std::string ();

    const char* const locked =
      (char *)LockResource (license_ref);

    if (locked != nullptr)
    {
      std::string str ( locked, res_size );
      UnlockResource  ( locked );
      return      str;
    }
  }

  return std::string ();
}

// PlugIns may hook this to insert additional EULA terms
__declspec (noinline)
void
__stdcall
SK_ImGui_DrawEULA_PlugIn (LPVOID reserved)
{
  UNREFERENCED_PARAMETER (reserved);

  // Need a minimal function body to avoid compiler optimization
  ImGuiIO& io =
    ImGui::GetIO ();

  DBG_UNREFERENCED_LOCAL_VARIABLE (io);
}

extern std::wstring&
__stdcall
SK_GetPluginName (void);

void
__stdcall
SK_ImGui_DrawEULA (LPVOID reserved)
{
  if (! ImGui::GetFont ())
    return;


  //extern uint32_t __stdcall SK_Steam_PiratesAhoy (void);
  extern AppId64_t __stdcall SK_SteamAPI_AppID    (void);

  ImGuiIO& io =
    ImGui::GetIO ();

  struct show_eula_s {
    bool show;
    bool never_show_again;
  };

  static float last_width  = -1;
  static float last_height = -1;

  if (last_width != io.DisplaySize.x || last_height != io.DisplaySize.y)
  {
    SK_ImGui_SetNextWindowPosCenter (ImGuiCond_Always);
    last_width = io.DisplaySize.x; last_height = io.DisplaySize.y;
  }


  ImVec2 scaler (io.FontGlobalScale, io.FontGlobalScale);

  ImGui::SetNextWindowSizeConstraints (ImVec2 (780.0f * scaler.x,
                                               350.0f * scaler.y), ImVec2 ( 0.925f * io.DisplaySize.x * scaler.x,
                                                                            0.925f * io.DisplaySize.y * scaler.y ) );

  static std::wstring
              plugin        = SK_GetPluginName ();
  static char szTitle [256] = { };
  static bool open          = true;

  if (*szTitle == '\0')
    sprintf (szTitle, "%ws Software License Agreement", plugin.c_str ());

  if (((show_eula_s *)reserved)->show)
    ImGui::OpenPopup (szTitle);

#ifdef _ProperSpacing
  const  float font_size = ImGui::GetFont  ()->FontSize * io.FontGlobalScale;
#endif

  if (ImGui::BeginPopupModal (szTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize /*| ImGuiWindowFlags_NavFlattened*/))
  {
    SK_ImGui_SetNextWindowPosCenter (ImGuiCond_Appearing);
    ImGui::SetNextWindowFocus       ();
    ImGui::FocusWindow              (ImGui::GetCurrentWindow ());

    //if (io.DisplaySize.x < 1024.0f || io.DisplaySize.y < 720.0f)
    //{
    //  ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (1.0f, 0.6f, 0.2f, 1.0f));
    //  ImGui::Bullet   ();
    //  ImGui::SameLine ();
    //  ImGui::TextWrapped (
    //       "This software only runs at resolutions >= 1024x768 or 1280x720, please uninstall the software or use a higher resolution than (%lux%lu).",
    //         (int)io.DisplaySize.x, (int)io.DisplaySize.y
    //  );
    //  ImGui::PopStyleColor ();
    //  goto END_POPUP;
    //}

    /* --- shhh
    bool pirate = ( SK_SteamAPI_AppID    () != 0 &&
                    SK_Steam_PiratesAhoy () != 0x0 );
    */

    ImGui::BeginGroup ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.7f, 0.7f, 0.7f, 1.0f));
    //
    // 4/8/19: Corrected misleading language that may have implied DRM to some.
    //
    //    It has always and will always be permissible to compile the project from
    //      source and do anything you want with it; the EULA is intended only to
    //        indicate uses that will receive support from the developers.
    //
    ImGui::TextWrapped (
         "Pre-compiled distributions of Special K only support products in their"
         " original form.\n"
    );

    ImGui::PushStyleColor (ImGuiCol_Text, ImVec4 (0.9f, 0.9f, 0.15f, 1.0f));
    ImGui::Bullet         ();
    ImGui::SameLine       ();
    ImGui::TextWrapped    (
      "Modifying copyright circumvented software is unsupported and probably"
      " unstable."
    );
    ImGui::PopStyleColor  (2);

    ImGui::Separator ();
    ImGui::EndGroup  ();


    ImGui::BeginChild ("EULA_Body",   ImVec2 (0.0f, ImGui::GetFrameHeightWithSpacing () * 15.0f),   false);
    ImGui::BeginGroup ();
    ImGui::BeginChild ("EULA_Body2",  ImVec2 (0.0f, ImGui::GetFrameHeightWithSpacing () * 13.666f), false, ImGuiWindowFlags_NavFlattened);

    /* --- shhh
    if (ImGui::CollapsingHeader (pirate ? "Overview of Products Unsupported" :
                                          "Overview of Products Licensed"))
    */
    if (ImGui::CollapsingHeader ("Overview of Products Licensed"))
    {
      SK_ImGui_AutoFont fixed_font (
        ImGui::GetIO ().Fonts->Fonts [1]
      );
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_OVERVIEW).c_str ());
    }

    ImGui::Separator  ();

    SK_ImGui_DrawEULA_PlugIn (reserved);

    if (ImGui::CollapsingHeader ("7-Zip"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_7ZIP).c_str ());
    }

    if (config.apis.ADL.enable && ImGui::CollapsingHeader ("ADL"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_ADL).c_str ());
    }

    if ( ((int)SK_GetCurrentRenderBackend ().api &  (int)SK_RenderAPI::D3D11 ||
               SK_GetCurrentRenderBackend ().api ==      SK_RenderAPI::D3D12 ) &&
         ImGui::CollapsingHeader ("DirectXTex (D3D11/12)")
       )
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_DXTEX).c_str ());
    }

    if (ImGui::CollapsingHeader ("FreeType 2"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_FREETYPE2).c_str ());
    }


    //if ( SK_GetCurrentRenderBackend ().api == SK_RenderAPI::OpenGL &&
    //       ImGui::CollapsingHeader ("GLEW (OpenGL)")
    //   )
    //{
    //  ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_GLEW).c_str ());
    //}

    if (ImGui::CollapsingHeader ("GLM v 0.9.4.5"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_GLM_0_9_4_5).c_str ());
    }

    if (ImGui::CollapsingHeader ("ImGui"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_IMGUI).c_str ());
    }

    if (ImGui::CollapsingHeader ("fontawesome"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_FONTAWESOME).c_str ());
    }

    if (ImGui::CollapsingHeader ("imgui-filebrowser"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_FILEBROWSER).c_str ());
    }

    if (ImGui::CollapsingHeader ("nlohmann-json"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_NLOHMANN_JSON).c_str ());
    }

    if (ImGui::CollapsingHeader ("MinHook"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_MINHOOK).c_str ());
    }

    if (ImGui::CollapsingHeader ("OpenLibSys"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_OPENLIBSYS).c_str ());
    }

    if (config.apis.NvAPI.enable && ImGui::CollapsingHeader ("NvAPI"))
    {
      SK_ImGui_AutoFont fixed_font (
        ImGui::GetIO ().Fonts->Fonts [1]
      );
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_NVAPI).c_str ());
    }

    if (ImGui::CollapsingHeader ("Special K"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_SPECIALK).c_str ());
    }

    if (ImGui::CollapsingHeader ("STB"))
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_STB).c_str ());
    }


    if ( SK_GetCurrentRenderBackend ().api == SK_RenderAPI::Vulkan &&
           ImGui::CollapsingHeader ("Vulkan")
       )
    {
      ImGui::TextWrapped ("%s", SK_GetLicenseText (IDR_LICENSE_VULKAN).c_str ());
    }

    ImGui::EndChild (); // EULA_Inset
    ImGui::BeginChild ("EULA_Inset", ImVec2 (0.0f, ImGui::GetFrameHeightWithSpacing () * 1.1616f), false, ImGuiWindowFlags_NavFlattened);
    ImGui::Separator  ();

    ImGui::Columns  (2, "", false);
    ImGui::TreePush (   "");

    if (ImGui::Button (" Decline "))
    {
      ExitProcess (0x00);
    }

    if (ImGui::IsItemHovered ())
    {
      ImGui::BeginTooltip ();
      ImGui::Bullet       ();                                              ImGui::SameLine ();
      ImGui::TextColored  (ImVec4 (1.0f, 1.0f, 0.0f, 1.0f), "WARNING:  "); ImGui::SameLine ();
      ImGui::TextColored  (ImVec4 (0.9f, 0.9f, 0.9f, 1.0f), "Game will exit!");
      ImGui::EndTooltip   ();
    }

    ImGui::TreePop    ();
    ImGui::NextColumn ();

    /* --- shhh 
    if (! pirate)
    {
      ImGui::Checkbox ("I agree ... never show me this again!", &((show_eula_s *)reserved)->never_show_again);
      ImGui::SameLine ();
    }

    if (ImGui::Button (" Accept ") && (! pirate))
    */
    ImGui::Checkbox ("I agree ... never show me this again!", &((show_eula_s *)reserved)->never_show_again);
    ImGui::SameLine ();

    if (ImGui::Button (" Accept "))
    {
      ImGui::CloseCurrentPopup ();

      open = false;
      ((show_eula_s *)reserved)->show = open;

      config.imgui.show_eula = ! ((show_eula_s *)reserved)->never_show_again;

      const wchar_t* config_name = SK_GetBackend ();

      if (SK_IsInjected ())
        config_name = L"SpecialK";

      if (((show_eula_s*)reserved)->never_show_again)
      {
        app_cache_mgr->setLicenseRevision (SK_LICENSE_REVISION);
        app_cache_mgr->saveAppCache       ();
      }

      SK_SaveConfig (config_name);
    }

    ImGui::SetItemDefaultFocus ();

    /* --- shhh
    if (pirate && ImGui::IsItemHovered ())
    {
      ImGui::BeginTooltip ();

      ImGui::TextColored (ImColor (255,255,255), "Corrupted Steamworks Platform Files Encountered");
      ImGui::Separator   (  );
      ImGui::BulletText  ("Unable to validate important mod files because steam_api(64).dll is invalid.");
      ImGui::TreePush    ("");
      ImGui::BulletText  ("If you think this is in error, re-validate your game through Steam and try again.");
      ImGui::TreePop     (  );

      ImGui::BulletText  ("I do not support altered steam_api(64).dll files for stability reasons");
      ImGui::TreePush    ("");
      ImGui::BulletText  ("You must disable SteamAPI (set [Steam.Log] Silent=true) and please do not waste my time asking for support.");
      ImGui::BulletText  ("Pirates must seek support elsewhere.");
      ImGui::TreePop     (  );

      ImGui::EndTooltip  ();
    }
    */

    ImGui::EndChild (); // EULA_Inset
    ImGui::EndGroup ();
    ImGui::EndChild (); // EULA_Body2
    ImGui::EndPopup ();
  }
}