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
 * Set the installer executable version information.
 */

VIProductVersion "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Steam Content Server Limiter"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "© Nigel Bree <nigel.bree@gmail.com>"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Steam Content Server Limiter Install"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}"

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
 * respect (as it was rewritten from scratch for 0.2 onwards and the author has
 * never documented the new version). This one aspect of the old documentation
 * seems to still work, though.
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
  IfFileExists $INSTDIR\steamlimit.exe 0 freshInstall
    !insertmacro UAC_AsUser_Call Function quitProgram ${UAC_SYNCINSTDIR}
    goto upgrade

freshInstall:
  /*
   * By default, run at login. Since our app is so tiny, this is hardly
   * intrusive and undoing it is a simple context-menu item.
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

  WriteRegStr HKCU "Software\SteamLimiter" "LastVersion" \
                   "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"
  WriteRegStr HKCU "Software\SteamLimiter" "NextVersion" \
                   "${VER_MAJOR}.${VER_MINOR}.${VER_BUILD}.${VER_REV}"

  /*
   * See if there's a 0.3.0 filter setting inside HKLM - if so, move it to the
   * HKCU key and launch the monitor app.
   */

  ReadRegStr $0 HKLM "Software\SteamLimiter" "Server"
  IfErrors 0 setServerValue

  /*
   * See if there's an existing setting under HKCU - if so, preserve it and
   * just move on to launching the monitor app.
   */

  ReadRegStr $0 HKCU "Software\SteamLimiter" "Server"
  IfErrors 0 finishInstall

  /*
   * We don't have an existing setting - try and auto-configure the right
   * one based on detecting the upstream ISP using a web service. There's
   * a small script to do this which we can run (elevated if necessary).
   */

  ExecWait 'wscript "$INSTDIR\setfilter.js"'
  IfErrors 0 finishInstall

  /*
   * This value is for TelstraClear; use it if we have to.
   */

  StrCpy $0 "203.167.129.4"

setServerValue:
  WriteRegStr HKCU "Software\SteamLimiter" "Server" $0

finishInstall:
  DeleteRegKey HKLM "Software\SteamLimiter"
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
  DeleteRegKey HKLM "Software\SteamLimiter"
  DeleteRegKey HKCU "Software\SteamLimiter"
SectionEnd
