/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * This application looks for a running instance of Valve's Steam to inject our
 * limiter into.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2011-2012 Nigel Bree; All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "inject.h"
#include "hyperlink.h"
#include "profile.h"
#include "../nolocale.h"

#include "resource.h"
#include <shellapi.h>
#include <stdlib.h>

/**
 * Simple cliches for measuring arrays.
 */

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))

/**
 * Various internal-use window messages.
 */

#define WM_NOTIFYICON   (WM_USER + 1)
#define WM_SUSPEND      (WM_USER + 2)
#define WM_SUSPENDED    (WM_USER + 3)

/**
 * Globally record the application's current path.
 */

wchar_t       * g_appPath;

/**
 * A string version of the application version data.
 */

wchar_t       * g_appVer;

/**
 * If filtering is (temporarily, anyway) currently disabled.
 */

bool            g_filterDisabled;

/**
 * The last known steam process ID we have worked with.
 *
 * This is stored mainly to stop repeated attempts to work on the same process
 * instance; setting it to 0 will cause the filter settings to be reapplied on
 * the next poll attempt.
 */

unsigned long   g_steamProcess;

/**
 * If our "about" window is visible, the window should be here.
 */

HWND            g_aboutWindow;

/**
 * Put the profile-picker window here.
 */

HWND            g_profileWindow;

/**
 * The last known time that we checked for an upgraded version.
 */

ULONGLONG       g_upgradeCheckTime;

/**
 * The currently selected profile ID, which determines the filter to use.
 */

unsigned long   g_profileId;

/**
 * How often to check for upgrades after being installed.
 *
 * This doesn't cause an in-your-face prompt, so more often than less is not
 * likely to be harmful. The only potential drawback here would be for anyone
 * on a dial-up connection. Once a week seems good.
 *
 * This is based on a FILETIME, so 100ns is the basic "tick" aka 2.5 million,
 * we'll put the number of days as the leftmost term.
 */

#define UPGRADE_CHECK_DELTA     (7 * 24 * 60 * 60 * 10000000ULL)

/**
 * The standard Windows shell key for application launch at login.
 */

#define WINDOWS_RUN     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE       L"SteamLimiter"

/**
 * General limiter settings registry paths.
 *
 * @{
 */

#define LIMIT_SETTINGS  L"Software\\" RUN_VALUE
#define DISABLE_VALUE   L"Disabled"
#define VERSION_VALUE   L"NextVersion"
#define TIMESTAMP_VALUE L"UpgradeCheck"
#define PROFILE_VALUE   L"Profile"

#define REPLACE_SETTINGS        LIMIT_SETTINGS L"\\Replace"

/**@}*/

/**
 * The application's setting root key.
 */

RegKey          g_settings (LIMIT_SETTINGS);


/**
 * Set the monitor process to autostart, or disable it.
 */

void setAutostart (bool state) {
        RegKey (WINDOWS_RUN) [RUN_VALUE] <<= (state ? g_appPath : 0);
}

/**
 * Retrieve the current auto-start state.
 */

bool getAutoStart (void) {
        bool            value;
        RegKey (WINDOWS_RUN) [RUN_VALUE] >>= value;
        return value;
}

/**
 * Use ShellExecute to fire off a URL, especially for the steam:// URL scheme.
 */

void runCommand (const wchar_t * command, const wchar_t * parameters = 0,
                 bool wait = false) {
        SHELLEXECUTEINFOW exec = { sizeof (exec) };
        exec.lpFile = command;
        exec.lpParameters = parameters;

        if (wait) {
                /*
                 * Ask for the process handle so we can wait on it.
                 */

                exec.fMask |= SEE_MASK_NOCLOSEPROCESS;
                SetCursor (LoadCursor (0, IDC_WAIT));
        }

        /*
         * Force the current directory to be the application directory.
         */

        wchar_t         path [1024];
        wcscpy_s (path, ARRAY_LENGTH (path), g_appPath);

        wchar_t       * end = wcsrchr (path, '\\');
        if (end == 0)
                return;
        * end = 0;

        exec.lpDirectory = path;
        ShellExecuteExW (& exec);

        if (! wait)
                return;

        WaitForSingleObject (exec.hProcess, INFINITE);
        CloseHandle (exec.hProcess);

        /*
         * Restore the Windows cursor back from the hourglass; using this will
         * cause it to redetect the right cursor even if it got moved during
         * the wait.
         */

        POINT           point;
        GetCursorPos (& point);
        SetCursorPos (point.x, point.y);
}

/**
 * Poll for Steam application instances.
 *
 * While we're at it, sometimes decide whether to check our webservice for
 * updates.
 */

void steamPoll (bool attach) {
        ULONGLONG       now;
        GetSystemTimeAsFileTime ((FILETIME *) & now);

        if (now - g_upgradeCheckTime > UPGRADE_CHECK_DELTA) {
                g_upgradeCheckTime = now;
                g_settings [TIMESTAMP_VALUE] <<= now;
                runCommand (L"wscript.exe", L"setfilter.js");
        }

        /*
         * Look for a Steam window. There can be several of these; the one we
         * care about will be associated with steam.exe if we open the process
         * and query the module. The Steam window itself has no children and
         * a useless class name of the form USurface_xxxxx so it's hard to make
         * a clean distinction between it and other windows named Steam.
         */

        HWND            steam = 0;

        unsigned long   processId;
        unsigned long   threadId;

        for (;;) {
                steam = FindWindowExW (0, steam, 0, L"Steam");
                if (steam == 0)
                        return;

                /*
                 * Get the owning process so we can query and manipulate it.
                 */

                processId = 0;
                threadId = GetWindowThreadProcessId (steam, & processId);
                if (threadId == 0 && processId == 0)
                        continue;

                /*
                 * Verify that this is really the Steam window. We can do this
                 * in a range of ways, depending on how friendly we want to
                 * get with the process.
                 *
                 * One obvious method is to use GetModuleFileNameEx () or the
                 * like, but that means taking a dependency on PSAPI.DLL for
                 * Windows XP support. So, comparing the Window class it is.
                 */

                wchar_t         buf [80];
                unsigned long   length;
                length = GetClassNameW (steam, buf, ARRAY_LENGTH (buf));

static  wchar_t         className [] = { L"USurface" };

                if (length < ARRAY_LENGTH (className) - 1)
                        continue;
                if (wcsncmp (buf, className, ARRAY_LENGTH (className) - 1) != 0)
                        continue;

                break;
        }

        if (attach && processId == g_steamProcess)
                return;

        /*
         * Don't load the unload routine repeatedly if we're disabled; do it
         * at most once until we have a successful load; we want to try an
         * unload speculatively in case there's a stale filter DLL attached
         * the first time we try, but otherwise there's no point.
         */

static  unsigned long   unloadCount;

        if (g_filterDisabled && unloadCount > 0)
                return;

        /*
         * Verify that the process ID actually belongs to steam.exe, and isn't
         * for example a Steam window opened by explorer.exe.
         */

        if (! attach || g_filterDisabled) {
                callFilterId (processId, "FilterUnload");
                g_steamProcess = 0;
                ++ unloadCount;
                return;
        }

        if (g_steamProcess != 0)
                callFilterId (g_steamProcess, "FilterUnload");

        Profile         current (g_profileId, & g_settings);

        unloadCount = 0;

        if (! callFilterId (processId, "SteamFilter", current.filter (),
                            HKEY_CURRENT_USER, REPLACE_SETTINGS))
                return;

        /*
         * The injection succeeded.
         */

        g_steamProcess = processId;
}

/**
 * Set the enable state for the filter.
 */

void setFilter (bool state) {
        g_settings [DISABLE_VALUE] <<= (state ? L"1" : 0);

        /*
         * Cause the filter setting to be applied to any existing filter
         * instance.
         */

        g_filterDisabled = state;
        steamPoll (! state);
}

/**
 * Get the current enable state for the filter.
 */

bool getFilter (void) {
        bool            value;
        g_settings [DISABLE_VALUE] >>= value;
        return value;
}

/**
 * Add a set of strings from resources into a combo-box or list box.
 */

void addStrings (HWND window, UINT control, UINT start) {
        HWND            ctrl = GetDlgItem (window, control);

        /*
         * Add strings to the dialog's combo box
         */

        wchar_t         text [1024];
        unsigned long   id = start;
        HINSTANCE       self = GetModuleHandle (0);

        for (;; ++ id) {
                int             length;
                length = LoadStringW (self, id,
                                      text, ARRAY_LENGTH (text));
                if (length == 0)
                        break;

                LRESULT         result;
                result = SendMessage (ctrl, CB_ADDSTRING, 0, (LPARAM) text);

                if (result == LB_ERR)
                        break;
        }
}

/**
 * Context menu for the system notification area icon.
 */

HMENU           g_contextMenu;

/**
 * Show the notification area icon's context menu.
 */

void showContextMenu (HWND window) {
        /*
         * Set the check state of any toggle menu items.
         */

        MENUITEMINFO    info = { sizeof (info) };
        info.fMask = MIIM_STATE;
        info.fState = getAutoStart () ? MFS_CHECKED : 0;
        SetMenuItemInfo (g_contextMenu, ID_CONTEXT_AUTOSTART, 0, & info);

        /*
         * We store a "disabled" flag but show enabled, so this is complemented
         * in the get/set logic relative to the autostart setting.
         */

        info.fState = getFilter () ? 0 : MFS_CHECKED;
        SetMenuItemInfo (g_contextMenu, ID_CONTEXT_ENABLED, 0, & info);

        POINT           point;
        GetCursorPos (& point);

        SetForegroundWindow (window);

        TrackPopupMenu (g_contextMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, 
                        point.x, point.y, 0, window, NULL); 
}

/**
 * Dialog procedure for an "about" dialog.
 *
 * If I wanted to get fancy, I'd subclass the dialog and override WC_NCHITTEST
 * but that's altogether too much work :-)
 */

INT_PTR CALLBACK aboutProc (HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
        unsigned short  item = LOWORD (wparam);
        unsigned short  code = HIWORD (wparam);
        Hyperlink     * link;

        switch (message) {
        case WM_INITDIALOG: {
                /*
                 * Provide some text for the taskbar, since setting a caption
                 * in the dialog resource forces a window title.
                 */

                wchar_t         title [1024];
                LoadString (GetModuleHandle (0), IDS_ABOUT,
                            title, ARRAY_LENGTH (title));
                SetWindowText (window, title);

                /*
                 * Subclass the IDC_SITE control.
                 */

                Hyperlink :: attach (window, IDC_SITE);
                Hyperlink :: attach (window, IDC_AUTHOR);
                Hyperlink :: attach (window, IDC_FEEDBACK);
                break;
            }

        case WM_NCHITTEST: {
                /*
                 * Make the entire dialog draggable, since there's no caption.
                 */

                RECT            rect;
                GetWindowRect (window, & rect);
                POINT           point = { LOWORD (lparam), HIWORD (lparam) };
                if (PtInRect (& rect, point)) {
                        SetWindowLong (window, DWLP_MSGRESULT, HTCAPTION);
                        return TRUE;
                }
                break;
            }

        case WM_COMMAND:
                switch (item) {
                case IDC_SITE:
                case IDC_AUTHOR:
                case IDC_FEEDBACK:
                case IDOK:
                case IDB_UPGRADE:
                        if (code != BN_CLICKED)
                                item = 0;
                        break;

                default:
                        item = 0;
                        break;
                }

                if (item == 0)
                        break;

                link = Hyperlink :: at (window, item);
                if (link != 0) {
                        runCommand (link->link ());
                        return 0;
                }

                DestroyWindow (window);
                if (window == g_aboutWindow)
                        g_aboutWindow = 0;

                if (item == IDB_UPGRADE)
                        runCommand (L"wscript.exe", L"setfilter.js upgrade");

                return true;

        case WM_MOUSELEAVE:
                break;

        default:
                break;
        }

        return FALSE;
}

/**
 * Assistant for ShowWindow () for centered dialogs to handle multiple monitors.
 *
 * The DS_CENTER style evidently sometimes chooses poorly based on user reports
 * so I can either choose to force the use of the primary monitor, or the
 * monitor currently containing the mouse cursor. Which is really a matter of
 * preference, but I'm inclined towards forcing the primary monitor to avoid
 * any inconsistency that might happen given that the context menu position is
 * awkward and a mouse report can end up on a second monitor.
 *
 * See http://blogs.msdn.com/b/oldnewthing/archive/2007/08/09/4300545.aspx
 */

void showCentered (HWND window) {
        /*
         * Get the current window dimensions, since we'll preserve the width
         * and height.
         */

        RECT            srcRect;
        GetWindowRect (window, & srcRect);

        /*
         * The primary monitor is always at (0, 0)
         */

static  const POINT     home = { 0, 0 };
        HMONITOR        monitor = MonitorFromPoint (home, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO     info = { sizeof (info) };
        GetMonitorInfoW (monitor, & info);

        /*
         * This centers the incoming item by width and height; here I don't
         * assume the 0, 0 origin just in case I change the selection to not
         * always pick the primary.
         */

        unsigned long   width = srcRect.right - srcRect.left;
        unsigned long   height = srcRect.bottom - srcRect.top;

        unsigned long   destWidth = info.rcMonitor.right - info.rcMonitor.left;
        unsigned long   destHeight = info.rcMonitor.bottom - info.rcMonitor.top;

        unsigned long   left = info.rcMonitor.left + (destWidth - width) / 2;
        unsigned long   top = info.rcMonitor.top + (destHeight - height) / 2;

        SetWindowPos (window, 0, left, top, width, height,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow (window, SW_SHOW);
}


/**
 * Create and show the "About" window.
 */

void showAbout (void) {
        if (g_aboutWindow != 0) {
                SetFocus (g_aboutWindow);
                return;
        }

        wchar_t       * nextVersion = 0;
        g_settings [VERSION_VALUE] >>= nextVersion;

        wchar_t       * dialog = MAKEINTRESOURCE (IDD_ABOUT);
        bool            upgrade = false;

        while (nextVersion != 0) {
                /*
                 * String version comparison is tricky; for now, as a hack make
                 * version strings of equal length compare lexically, but make
                 * a longer string also able to win (for long build numbers or
                 * the like).
                 */

                int             diff = wcscmp (nextVersion, g_appVer);
                if (diff == 0)
                        break;
                if (diff < 0 && wcslen (nextVersion) <= wcslen (g_appVer))
                        break;

                upgrade = true;
                dialog = MAKEINTRESOURCE (IDD_ABOUT_UPGRADE);
                break;
        }

        free (nextVersion);

        g_aboutWindow = CreateDialogW (GetModuleHandle (0), dialog, 0, aboutProc);
        if (g_aboutWindow == 0)
                return;

        wchar_t         text [1024];
        GetDlgItemTextW (g_aboutWindow, IDC_APPNAME, text, ARRAY_LENGTH (text));

        wcscat_s (text, ARRAY_LENGTH (text), L" ");
        wcscat_s (text, ARRAY_LENGTH (text), g_appVer);

        SetDlgItemTextW (g_aboutWindow, IDC_APPNAME, text);

        showCentered (g_aboutWindow);
}

/**
 * Enable the right buttons in the profile dialog, since the detect and upload
 * buttons overlay each other in the dialog template and we only want one to be
 * visible at a time.
 */

void setProfileButtons (HWND window, int index) {
        if (index == Profile :: g_custom) {
                ShowWindow (GetDlgItem (window, IDC_AUTODETECT), SW_HIDE);
                ShowWindow (GetDlgItem (window, IDC_UPLOAD), SW_SHOW);
                return;
        }
        
        ShowWindow (GetDlgItem (window, IDC_UPLOAD), SW_HIDE);

        HWND            control = GetDlgItem (window, IDC_AUTODETECT);
        ShowWindow (control, SW_SHOW);
        EnableWindow (control, index != Profile :: g_noTraffic);
}

/**
 * Profile-selection dialog procedure.
 */

INT_PTR CALLBACK profileProc (HWND window, UINT message, WPARAM wparam,
                              LPARAM lparam) {
        switch (message) {
        case WM_INITDIALOG: {
                /*
                 * Provide some text for the taskbar, since setting a caption
                 * in the dialog resource forces a window title.
                 */

                wchar_t         title [1024];
                LoadString (GetModuleHandle (0), IDS_PICKPROFILE,
                            title, ARRAY_LENGTH (title));
                SetWindowText (window, title);

                Hyperlink :: attach (window, IDC_UPDATE_ABOUT);
                Hyperlink :: attach (window, IDC_FILTER_ABOUT);

                addStrings (window, IDCB_PROFILE, IDS_PROFILENAME);

                HWND            sel = GetDlgItem (window, IDCB_PROFILE);
                SendMessage (sel, CB_SETCURSEL, g_profileId, 0);

                Profile         current (g_profileId, & g_settings);
                current.toWindow (window);

                setProfileButtons (window, g_profileId);
                return 0;
            }

        case WM_NCHITTEST: {
                /*
                 * Make the entire dialog draggable, since there's no caption.
                 */

                RECT            rect;
                GetWindowRect (window, & rect);
                POINT           point = { LOWORD (lparam), HIWORD (lparam) };
                if (PtInRect (& rect, point)) {
                        SetWindowLong (window, DWLP_MSGRESULT, HTCAPTION);
                        return TRUE;
                }
                break;
            }

        case WM_COMMAND:
                break;

        default:
                return 0;
        }


        unsigned short  item = LOWORD (wparam);
        unsigned short  code = HIWORD (wparam);

        switch (code) {
        case BN_CLICKED: {
                Hyperlink     * link = Hyperlink :: at (window, item);
                if (link != 0) {
                        runCommand (link->link ());
                        return 0;
                }

                if (item == IDC_UPDATE)
                        return 0;

                if (item == IDC_AUTODETECT) {
                        /*
                         * Write to the "temp" profile, then load it into the
                         * window context.
                         */

                        runCommand (L"wscript.exe", L"setfilter.js", true);

                        Profile         current (Profile :: g_temp, & g_settings);
                        current.toWindow (window, false);
                        return 0;
                }
                if (item == IDC_UPLOAD) {
                        /*
                         * Only enabled for the "custom" profile.
                         */

                        SetCursor (LoadCursor (0, IDC_WAIT));
                        runCommand (L"wscript.exe", L"setfilter.js upload", true);
                        return 0;
                }

                break;
            }

        case CBN_SELCHANGE: {
                /*
                 * Switch the current profile.
                 */

                int             index;
                index = (int) SendMessage ((HWND) lparam, CB_GETCURSEL, 0, 0);

                Profile         change (index, & g_settings);
                change.toWindow (window);
                setProfileButtons (window, index);
                return 0;
            }

        default:
                return FALSE;
        }

        if (item != IDCANCEL) {
                /*
                 * Save the current profile.
                 */

                HWND            control = GetDlgItem (window, IDCB_PROFILE);
                int             index;
                index = (int) SendMessage (control, CB_GETCURSEL, 0, 0);

                Profile         change (index, & g_settings);

                change.fromWindow (window);
                change.toRegistry ();

                g_profileId = index;
                g_settings [PROFILE_VALUE] <<= g_profileId;

                steamPoll (false);
                steamPoll (true);
        }

        DestroyWindow (window);
        if (window == g_profileWindow)
                g_profileWindow = 0;

        return 0;
}

/**
 * Show the profile-selection dialog.
 */

void showProfile (void) {
        if (g_profileWindow != 0) {
                SetFocus (g_profileWindow);
                return;
        }

        HMODULE         self = GetModuleHandle (0);
        g_profileWindow = CreateDialogW (self, MAKEINTRESOURCE (IDD_PROFILE), 0,
                                         profileProc);

        showCentered (g_profileWindow);
}

/**
 * Window procedure for our basic window.
 */

LRESULT CALLBACK windowProc (HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
        switch (message) {
        case WM_CLOSE:
                DestroyWindow (window);
                PostQuitMessage (0);
                return 0;

        case WM_SUSPEND:
                DestroyWindow (window);
                steamPoll (false);

                PostThreadMessage (GetCurrentThreadId (), WM_SUSPENDED,
                                   wparam, lparam);
                return 1;

                /*
                 * Handle notification area icon actions.
                 */

        case WM_NOTIFYICON:
                switch (lparam) {
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                        showContextMenu (window);
                        break;

                default:
                        break;
                }

                return 0;

        case WM_COMMAND: {
                if (HIWORD (wparam) != 0)
                        break;

                unsigned short  item = LOWORD (wparam);
                MENUITEMINFO    info = { sizeof (info) };

                switch (item) {
                case ID_CONTEXT_EXIT:
                        PostQuitMessage (0);
                        break;

                case ID_CONTEXT_SHOWSTEAM:
                        runCommand (L"steam://nav/downloads");
                        break;

                case ID_CONTEXT_AUTOSTART:
                        info.fMask = MIIM_STATE;
                        GetMenuItemInfo (g_contextMenu, item, false,
                                         & info);

                        /*
                         * Get the complement of the previous state to toggle it.
                         */

                        setAutostart ((info.fState & MFS_CHECKED) == 0);
                        break;

                case ID_CONTEXT_ABOUT:
                        showAbout ();
                        break;

                case ID_CONTEXT_ENABLED:
                        info.fMask = MIIM_STATE;
                        GetMenuItemInfo (g_contextMenu, item, false,
                                         & info);
                        /*
                         * The setting as displayed is the complement of the
                         * registry, so set the displayed state to toggle it.
                         */

                        setFilter ((info.fState & MFS_CHECKED) != 0);
                        break;

                case ID_PROFILE_PICKER:
                        showProfile ();
                        break;

                default:
                        break;
                }
                return 0;
            }

        default:
                break;
        }

        return DefWindowProc (window, message, wparam, lparam);
}

/**
 * Get the full path to the application, and store it globally.
 */

wchar_t * getAppPath (void) {
        wchar_t         path [1024];
        unsigned long   length;
        length = GetModuleFileNameW (GetModuleHandle (0), path, ARRAY_LENGTH (path));
        if (length == 0)
                return 0;

        g_appPath = (wchar_t *) malloc ((length + 1) * sizeof (wchar_t));
        wcscpy_s (g_appPath, length + 1, path);

        /*
         * While we're at it, extract the actual app version string.
         */

        unsigned long   handle = 0;
        unsigned long   size = GetFileVersionInfoSize (g_appPath, & handle);

        void          * mem = malloc (size);
        GetFileVersionInfoW (g_appPath, handle, size, mem);

        VS_FIXEDFILEINFO * info;
        UINT            infoLength = 0;
        VerQueryValueW (mem, L"\\", (void **) & info, & infoLength);

        unsigned long   major = info->dwFileVersionMS >> 16;
        unsigned long   minor = info->dwFileVersionMS & 0xFFFF;
        unsigned long   build = info->dwFileVersionLS >> 16;
        unsigned long   rev = info->dwFileVersionLS & 0xFFFF;

        wsprintfW (path, L"%d.%d.%d.%d", major, minor, build, rev);

        length = wcslen (path) + 1;
        g_appVer = (wchar_t *) malloc (length * sizeof (wchar_t));
        wcscpy_s (g_appVer, length, path);

        return g_appPath;
}


/**
 * Wide version of WinMain.
 *
 * Here we want a simple application window that users can close to cease
 * monitoring Steam, and to kick off the actual monitoring.
 */

int CALLBACK wWinMain (HINSTANCE instance, HINSTANCE, wchar_t * command, int show) {
        bool            quit = false;
        bool            suspend = false;

        for (; __argc > 1 ; -- __argc, ++ __wargv) {
                wchar_t       * arg = __wargv [1];
                if (wcscmp (arg, L"-quit") == 0) {
                        quit = true;
                        continue;
                }

                if (wcscmp (arg, L"-debug") == 0 ||
                    wcscmp (arg, L"-suspend") == 0) {
                        suspend = true;
                        continue;
                }
        }

        /*
         * Look for an existing instance of the application.
         */

        HWND            window;
        window = FindWindowEx (0, 0, L"SteamMonitor", 0);

        while (window != 0) {
                if (suspend) {
                        LRESULT         result;
                        result = SendMessage (window, WM_SUSPEND,
                                              GetCurrentProcessId (), 0);

                        if (result != 0)
                                break;

                        /*
                         * If the target is an older version that doesn't have
                         * support for the new suspend operation, ask it to
                         * exit instead.
                         */

                        quit = true;
                }

                /*
                 * If there is an existing instance, we always exit; we may
                 * also ask the existing instance to close, and for the benefit
                 * of the invoking process we can wait until the window no
                 * longer exists.
                 */

                while (quit) {
                        SendMessageW (window, WM_CLOSE, 0, 0);

                        window = FindWindowEx (0, 0, L"SteamMonitor", 0);
                        if (window == 0)
                                break;

                        Sleep (100);
                }

                /*
                 * If we were quitting because of a failed suspend then we can
                 * go ahead and run rather than exit.
                 */

                if (! suspend)
                        return 0;

                quit = false;
                break;
        }

        if (quit)
                return 0;

        getAppPath ();

        /*
         * Load our icon resource.
         */

        HICON           myIcon;
        myIcon = LoadIconW (instance, MAKEINTRESOURCEW (IDI_APPICON));

        /*
         * Load the context menu.
         */

        HMENU           myMenu;
        myMenu = LoadMenuW (instance, MAKEINTRESOURCEW (IDR_CONTEXTMENU));
        g_contextMenu = GetSubMenu (myMenu, 0);

        /*
         * Create a window.
         */

        WNDCLASSEXW      classDef = {
                sizeof (classDef),
                0,
                windowProc,
                0, 0,
                instance,
                myIcon,
                0,
                GetSysColorBrush (COLOR_WINDOW),
                0,
                L"SteamMonitor",
                0               /* small icon */
        };

        ATOM            windowClass;
        windowClass = RegisterClassExW (& classDef);

        /*
         * Allow the application to restart itself here after suspending.
         */

reinitialize:
        int             style;
        style = WS_CAPTION | WS_POPUP | WS_SYSMENU;

        int             height;
        height = GetSystemMetrics (SM_CYCAPTION) * 2;

        /*
         * Register an icon for the system notification area.
         */

        NOTIFYICONDATA data = { sizeof (data) };
        data.hIcon = myIcon;
        data.uID = 0;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uVersion = NOTIFYICON_VERSION;
        data.uCallbackMessage = WM_NOTIFYICON;
        LoadStringW (instance, IDS_APPTITLE, data.szTip,
                     ARRAY_LENGTH (data.szTip));

        /*
         * Use WS_EX_TOOLWINDOW to prevent the window showing up in the taskbar
         * or ALT-TAB lists when active, and move it offscreen as well. This is
         * useful to allow the window to be active when the content menu for
         * the notification area icon is open, as TrackPopupMenu doesn't auto-
         * close cleanly if the parent window isn't up and about.
         *
         * If the window is located on-screen at 0,0 then while minimized it'll
         * also be under the taskbar, usually. But it still can potentially be
         * visible, so the cleanest thing to do is to move it offscreen when it
         * is being made visible for the menu.
         */

        window = CreateWindowExW (WS_EX_TOOLWINDOW, classDef.lpszClassName,
                                  data.szTip, style, -100, - height,
                                  200, height, 0, 0, 0, 0);

        if (window == 0)
                return 1;

        ShowWindow (window, SW_NORMAL);
        ShowWindow (window, SW_HIDE);
        
        data.hWnd = window;
        Shell_NotifyIconW (NIM_ADD, & data);

        /*
         * Figure out when the last upgrade check was run. If there's no record
         * of that in the registry yet, assume it's now.
         */

        g_settings [TIMESTAMP_VALUE] >>= g_upgradeCheckTime;
        if (g_upgradeCheckTime == 0) {
                GetSystemTimeAsFileTime ((FILETIME *) & g_upgradeCheckTime);
                g_settings [TIMESTAMP_VALUE] <<= g_upgradeCheckTime;
        }

        /*
         * Get the server IP to filter from the registry, along with the last
         * state of the enable/disable flag.
         */

        g_profileId = Profile :: g_home;
        g_settings [PROFILE_VALUE] >>= g_profileId;

        g_filterDisabled = getFilter ();

        unsigned short  release = 0;

        for (;;) {
                unsigned long   wait;
                wait = MsgWaitForMultipleObjects (0, 0, 0, 1000, QS_ALLINPUT);

                if (wait == WAIT_TIMEOUT) {
                        /*
                         * Poll for a Steam client running.
                         */

                        steamPoll (true);

                        /*
                         * After a number of continuous poll cycles, wipe the
                         * process working set to reduce memory load, at the
                         * cost of a few more page faults when we *are*
                         * interacted with to reinstate the Windows UI bits.
                         */

                        if (++ release != 10)
                                continue;

                        SetProcessWorkingSetSize (GetCurrentProcess (),
                                                  ~ 0UL, ~ 0UL);
                        continue;
                }

                release = 0;

                MSG             message;
                while (PeekMessage (& message, 0, 0, 0, TRUE)) {
                        if (message.message == WM_QUIT) {
                                quit = true;
                                break;
                        }
                        if (message.message == WM_SUSPENDED) {
                                Shell_NotifyIconW (NIM_DELETE, & data);
                                HANDLE          proc;
                                proc = OpenProcess (SYNCHRONIZE, false,
                                                    message.wParam);

                                if (proc != 0) {
                                        WaitForSingleObject (proc, INFINITE);
                                        CloseHandle (proc);
                                }

                                goto reinitialize;
                        }

                        if (g_aboutWindow != 0 &&
                            IsDialogMessageW (g_aboutWindow, & message)) {
                                continue;
                        }

                        if (g_profileWindow != 0 &&
                            IsDialogMessageW (g_profileWindow, & message)) {
                                continue;
                        }

                        TranslateMessage (& message);
                        DispatchMessage (& message);
                }

                if (quit) {
                        Shell_NotifyIconW (NIM_DELETE, & data);
                        steamPoll (false);
                        return 0;
                }
        }
}

/**@}*/
