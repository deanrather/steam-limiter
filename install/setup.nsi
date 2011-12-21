!include MUI.nsh
!include UAC.nsh

!ifndef OutDir
!define OutDir	"..\vs2010\release"
!endif

/*
 * Extract the designed version information from the resource header.
 */

!searchparse /file ..\limitver.h "#define VER_MAJOR       " VER_MAJOR
!searchparse /file ..\limitver.h "#define VER_MINOR       " VER_MINOR
!searchparse /file ..\limitver.h "#define VER_BUILD       " VER_BUILD
!searchparse /file ..\limitver.h "#define VER_REV         " VER_REV
!searchparse /file ..\limitver.h "#define VER_COMPANYNAME_STR " VER_AUTHOR
!searchparse /file ..\limitver.h "#define VER_WEBSITE_STR " VER_WEBSITE

/*
 * Describe the installer executable the NSIS compiler builds.
 */

OutFile ${OutDir}\steamlimit-${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}.exe
SetCompressor /SOLID lzma

Name "Steam Content Server Limiter"
RequestExecutionLevel user

InstallDir "$PROGRAMFILES\LimitSteam"

Icon ..\steamlimit\monitor.ico

/*
 * The script language requires custom variable declarations here, not in the
 * section where the variables are used.
 */

Var SETTINGS

/*
 * Set the installer executable version information.
 */

VIProductVersion "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Steam Content Server Limiter"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "© Nigel Bree <nigel.bree@gmail.com>"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Steam Content Server Limiter Install"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "Author" ${VER_AUTHOR}
VIAddVersionKey /LANG=${LANG_ENGLISH} "Website" ${VER_WEBSITE}

/*
 * Describe the installer flow, very minimal for us.
 */

Page License
LicenseData ..\LICENSE.txt

Page Directory
Page InstFiles

UninstPage UninstConfirm
UninstPage Instfiles

/*
 * For UAC-awareness, we ask the NSIS UAC plug-in to relaunch a nested copy of
 * the installer elevated, which can then call back function fragments as it
 * needs.
 *
 * This is per the UAC plugin wiki page, which is incorrect in most every other
 * respect (as it was rewritten from scratch for v0.2 of the plugin onwards and
 * the author has never documented the new version). This one aspect of the old
 * documentation seems to still work, though, and it's all we need.
 */

Function .onInit
  !insertmacro UAC_RunElevated
  ${Switch} $0
  ${Case} 0
    ${If} $1 == 1
      /*
       * The inner installer ran elevated.
       */
      quit
    ${ElseIf} $3 <> 0
      /*
       * We are the admin user already, let the outer install proceed.
       */

      ${Break}
    ${Endif}
    quit

  ${Default}
    quit

  ${EndSwitch}
FunctionEnd

Function runProgram
  Exec '"$INSTDIR\steamlimit.exe"'
FunctionEnd

Function quitProgram
  ExecWait '"$INSTDIR\steamlimit.exe" -quit'
FunctionEnd

Section
  StrCpy $SETTINGS "Software\SteamLimiter"

  IfFileExists $INSTDIR\steamlimit.exe 0 freshInstall
    !insertmacro UAC_AsUser_Call Function quitProgram ${UAC_SYNCINSTDIR}
    goto upgrade

freshInstall:
  /*
   * By default, run at login. Since our app is so tiny, this is hardly
   * intrusive and undoing it is a simple context-menu item.
	 *
	 * If upgrading, all this should still be in place so we leave it alone.
   */

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "SteamLimiter" \
                   "$\"$INSTDIR\steamlimit.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SteamLimiter" \
                   "DisplayName" "Steam Content Server Limiter"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SteamLimiter" \
                   "UninstallString" "$\"$INSTDIR\uninst.exe$\""

upgrade:
  /*
   * Before we write the new steamfilter.dll, deal with any stray copies of it
   * that are still referenced; rarely, versions prior to v0.5 would be able to
   * be confused by things like explorer.exe windows named "Steam" and so could
   * end up with the DLL injected in multiple places. Renaming the in-use DLL
   * works, so do that and then force it to be deleted.
   */

  Rename $INSTDIR\steamfilter.dll $INSTDIR\temp.dll
  Delete /REBOOTOK $INSTDIR\temp.dll

  SetOutPath $INSTDIR
  WriteUninstaller $INSTDIR\uninst.exe

  File ${OutDir}\steamlimit.exe
  File ${OutDir}\steamfilter.dll
  File ..\scripts\setfilter.js

  /*
   * Set the registry keys for the version options; from time to time we can
   * check the webservice to see if an update is available. Writing the current
   * version is a little redundant since it's in the monitor's resources, but
   * that's not always convenient to dig out in e.g. an upgrade script so there
   * seems little harm keeping another copy of the version string around.
   *
   * Of course, comparing *string* versions instead of *numeric* versions is
   * not always ideal. Since this is my own personal project, let's just say
   * that I'm expecting future me not to have to make that many distinct builds
   * and to be able to offload data changes to a webservice.
   */

  WriteRegStr HKCU $SETTINGS "LastVersion" \
                   "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"
  WriteRegStr HKCU $SETTINGS "NextVersion" \
                   "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"

  /*
   * See if there's a 0.3.0 filter setting inside HKLM - if so, move it to the
   * HKCU key and launch the monitor app.
   */

  ReadRegStr $0 HKLM $SETTINGS "Server"
  IfErrors 0 gotServerValue

  /*
   * See if there's an existing setting under HKCU - if so, preserve it and
   * just move on to launching the monitor app.
   */

  ReadRegStr $0 HKCU $SETTINGS "Server"
  IfErrors detectHomeProfile

gotServerValue:
  /*
   * The existing server setting can get migrated to the "custom" profile, and
   * unless it's one we know already we can select the custom profile as well.
   * If it's one of the 3 ones baked into pre-v0.5 installs we can take that as
   * a sign to use the "home" profile instead as we do for fresh installs.
   *
   * Most of the pre-0.5 installs which need custom servers are in Australia
   * and the new server-side filters should support them better (as they all
   * allow multiple servers to be used), but users should discover the "home"
   * profile option reasonably quickly.
   */

  WriteRegStr HKCU "$SETTINGS\C" "Filter" $0

  StrCmp $0 "203.167.129.4" 0 notTelstra

  WriteRegStr HKCU "$SETTINGS\C" "Country" "NZ"
  WriteRegStr HKCU "$SETTINGS\C" "ISP" "TelstraClear New Zealand"
  goto detectHomeProfile

notTelstra:
  StrCmp $0 "219.88.241.90" 0 notOrcon

  WriteRegStr HKCU "$SETTINGS\C" "Country" "NZ"
  WriteRegStr HKCU "$SETTINGS\C" "ISP" "Orcon New Zealand"
  goto detectHomeProfile

notOrcon:
  StrCmp $0 "202.124.127.66" 0 notSnap

  WriteRegStr HKCU "$SETTINGS\C" "Country" "NZ"
  WriteRegStr HKCU "$SETTINGS\C" "ISP" "Snap! New Zealand"
  goto detectHomeProfile

notSnap:
  /*
   * Stick with the custom profile.
   */
	 
  WriteRegStr HKCU "$SETTINGS\C" "Country" "AU"
  WriteRegStr HKCU "$SETTINGS\C" "ISP" "Unknown"
  WriteRegDWORD HKCU $SETTINGS "Profile" 3

detectHomeProfile:
  /*
   * We don't have an existing setting - try and auto-configure the right
   * one based on detecting the upstream ISP using a web service. There's
   * a small script to do this which we can run (elevated if necessary).
   */

  ExecWait 'wscript "$INSTDIR\setfilter.js" install'
  IfErrors 0 setProfile
    /*
     * This value is for TelstraClear; use it if we have to.
     */

    StrCpy $0 "*:27030=wlgwpstmcon01.telstraclear.co.nz"
    WriteRegStr HKCU "$SETTINGS\A" "Filter" $0
    WriteRegStr HKCU "$SETTINGS\A" "Country" "NZ"
    WriteRegStr HKCU "$SETTINGS\A" "ISP" "TelstraClear New Zealand"

setProfile:
  ReadRegDWORD $0 HKCU $SETTINGS "Profile"
  IfErrors 0 finishInstall
    /*
     * If there's an existing profile selection, leave it.
     * Otherwise, default to the "home" profile.
     */

    WriteRegDWORD HKCU $SETTINGS "Profile" 1

finishInstall:
  /*
   * Remove all the pre-v0.4 settings.
   */

  DeleteRegKey HKLM $SETTINGS
	
	/*
	 * Remove pre-v0.5 settings that were moved to profile options.
	 */

	DeleteRegValue HKCU $SETTINGS "Server"
	DeleteRegValue HKCU $SETTINGS "ISP"

  !insertmacro UAC_AsUser_Call Function runProgram ${UAC_SYNCINSTDIR}
SectionEnd

Section "Uninstall"
  ExecWait "$INSTDIR\steamlimit.exe -quit"

  Delete $INSTDIR\uninst.exe
  Delete $INSTDIR\steamlimit.exe
  Delete $INSTDIR\steamfilter.dll
  RMDir $INSTDIR
  
  DeleteRegValue HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" SteamLimit
  DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" SteamLimit
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LimitSteam"
  DeleteRegKey HKLM $SETTINGS
  DeleteRegKey HKCU $SETTINGS
SectionEnd
