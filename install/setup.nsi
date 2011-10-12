!include MUI.nsh

OutFile ..\vs2010\release\steamlimit-0.2.0.0.exe
SetCompressor /SOLID lzma

Name "Steam Content Server Limiter"

InstallDir "$PROGRAMFILES\LimitSteam"

Page License
LicenseData ..\LICENSE.txt

Page Directory
Page InstFiles

UninstPage UninstConfirm
UninstPage Instfiles

Icon ..\steamlimit\monitor.ico

VIProductVersion "0.2.0.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Steam Content Server Limiter"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "© Nigel Bree <nigel.bree@gmail.com>"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Steam Content Server Limiter Install"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "0.2.0"

Section
	IfFileExists $INSTDIR\steamlimit.exe 0 freshInstall
  	ExecWait "$INSTDIR\steamlimit.exe -quit"
    goto upgrade

freshInstall:
  /*
	 * By default, run at login. Since our app is so tiny, this is hardly
	 * intrusive and undoing it is a simple context-menu item.
	 */

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "SteamLimiter" \
									 "$\"$INSTDIR\steamlimit.exe$\""
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SteamLimiter" \
                   "DisplayName" "Steam Content Server Limiter"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\SteamLimiter" \
                   "UninstallString" "$\"$INSTDIR\uninst.exe$\""

upgrade:
  /* Set the default content server to limit to if not set. */
  ReadRegStr $0 HKLM "Software\SteamLimiter" "Server"
	IfErrors 0 replaceFiles
	
	/*
	 * This value is for TelstraClear; if I wanted to get fancy I'd have a menu
	 * page in the wizard for selecting alternatives, but I'm writing this for me
	 * to start with.
	 */

	WriteRegStr HKLM "Software\SteamLimiter" "Server" "203.167.129.4"

replaceFiles:
  SetOutPath $INSTDIR
	WriteUninstaller $INSTDIR\uninst.exe
  File ..\vs2010\release\steamlimit.exe
  File ..\vs2010\release\steamfilter.dll
  Exec "steamlimit.exe"
SectionEnd

Section "Uninstall"
	ExecWait "$INSTDIR\steamlimit.exe -quit"

  Delete $INSTDIR\uninst.exe
  Delete $INSTDIR\steamlimit.exe
	Delete $INSTDIR\steamfilter.dll
	RMDir $INSTDIR
	
  DeleteRegValue HKLM "SOFTWARE\Microsoft\Windows\CurrentVersion\Run" SteamLimit
	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\LimitSteam"
	DeleteRegKey HKLM "Software\SteamLimiter"
SectionEnd
