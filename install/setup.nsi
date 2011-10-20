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
 * For UAC-awareness, we ask the NSIS UAC plug-in to relauch a nested copy of
 * the installer elevated, which can then call back function fragments is it
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
  Exec "$INSTDIR\steamlimit.exe"
FunctionEnd

Function quitProgram
  ExecWait "$INSTDIR\steamlimit.exe -quit"
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
  /* See if there's a 3.0.0 setting inside HKLM */
  ReadRegStr $0 HKCU "Software\SteamLimiter" "Server"
  IfErrors 0 setServerValue

  /* Set the default content server to limit to if not set. */
  ReadRegStr $0 HKCU "Software\SteamLimiter" "Server"
  IfErrors 0 replaceFiles

  /*
   * This value is for TelstraClear; if I wanted to get fancy I'd have a menu
   * page in the wizard for selecting alternatives, but I'm writing this for me
   * to start with.
   *
   * From 0.3.0 onwards there is UI in the monitor program for this, so the
   * installer can probably stay as-is.
   */

  StrCpy $0 "203.167.129.4"

setServerValue:
  WriteRegStr HKCU "Software\SteamLimiter" "Server" $0

replaceFiles:
  DeleteRegKey HKLM "Software\SteamLimiter"

  SetOutPath $INSTDIR
  WriteUninstaller $INSTDIR\uninst.exe
  File ${OutDir}\steamlimit.exe
  File ${OutDir}\steamfilter.dll
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
