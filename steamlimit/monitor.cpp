/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * This application looks for a running instance of Valve's Steam to inject our
 * limiter into.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2011 Nigel Bree; All Rights Reserved.
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

wchar_t       * appPath;

/**
 * A string version of the application version data.
 */

wchar_t       * appVer;

/**
 * If filtering is (temporarily, anyway) currently disabled.
 */

bool            filterDisabled;

/**
 * The IP of the server we wish to limit Steam to using.
 */

wchar_t       * serverName;

/**
 * The last known steam process ID we have worked with.
 *
 * This is stored mainly to stop repeated attempts to work on the same process
 * instance; setting it to 0 will cause the filter settings to be reapplied on
 * the next poll attempt.
 */

unsigned long   steamProcess;

/**
 * If our "about" window is visible, the window should be here.
 */

HWND            aboutWindow;

/**
 * If the server-picker window is visible, the window should be here.
 */

HWND            pickWindow;

/**
 * The last known time that we checked for an upgraded version.
 */

ULONGLONG       upgradeCheckTime;

/**
 * How often to check for upgrades after being installed.
 *
 * This doesn't cause an in-your-face prompt, so more often than less is not
 * likely to be harmful. The only potential drawback here would be for anyone
 * on a dial-up connection. 20 or 30 days, maybe?
 *
 * This is based on a FILETIME, so 400ns is the basic "tick" aka 2.5 million,
 * we'll put the number of days as the leftmost term.
 */

#define UPGRADE_CHECK_DELTA     (30 * 24 * 60 * 60 * 2500000ULL)

/**
 * The standard Windows shell key for application launch at login.
 */

#define WINDOWS_RUN     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE       L"SteamLimiter"

/**
 * General limiter settings registry paths.
 */

#define LIMIT_SETTINGS  L"Software\\" RUN_VALUE
#define DISABLE_VALUE   L"Disabled"
#define VERSION_VALUE   L"NextVersion"
#define TIMESTAMP_VALUE L"UpgradeCheck"

#define SERVER_VALUE    L"Server"

/**
 * Common code for managing simple settings.
 */

void setSetting (const wchar_t * path, const wchar_t * valueName,
                 const wchar_t * value) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_CURRENT_USER, path, 0, KEY_WRITE, & key);
        if (result != ERROR_SUCCESS)
                return;

        if (value != 0) {
                RegSetValueExW (key, valueName, 0, REG_SZ, (BYTE *) value,
                                (wcslen (value) + 1) * sizeof (wchar_t));
        } else
                RegDeleteValueW (key, valueName);

        RegCloseKey (key);
}

/**
 * Set a 64-bit value in the registry.
 */

void setValue (const wchar_t * path, const wchar_t * valueName,
               ULONGLONG * value) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_CURRENT_USER, path, 0, KEY_WRITE, & key);
        if (result != ERROR_SUCCESS)
                return;

        if (value != 0) {
                RegSetValueExW (key, valueName, 0, REG_QWORD, (BYTE *) value,
                                sizeof (* value));
        } else
                RegDeleteValueW (key, valueName);

        RegCloseKey (key);
}

/**
 * Common code for testing a simple (boolean) setting.
 */

bool testSetting (const wchar_t * path, const wchar_t * valueName) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_CURRENT_USER, path, 0, KEY_READ, & key);
        if (result != ERROR_SUCCESS)
                return false;

        unsigned long   type;
        unsigned long   length;
        result = RegQueryValueExW (key, valueName, 0, & type, 0, & length);

        RegCloseKey (key);

        return result == ERROR_SUCCESS && type == REG_SZ;
}

/**
 * Read a simple string setting.
 */

wchar_t * getSetting (const wchar_t * path, const wchar_t * valueName) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_CURRENT_USER, path, 0, KEY_READ, & key);
        if (result != ERROR_SUCCESS)
                return 0;

        unsigned long   type;
        unsigned long   length;
        result = RegQueryValueExW (key, valueName, 0, & type, 0, & length);
        if (result != ERROR_SUCCESS || type != REG_SZ) {
                RegCloseKey (key);
                return 0;
        }

        wchar_t       * value = (wchar_t *) malloc (length);
        result = RegQueryValueExW (key, valueName, 0, & type, (BYTE *) value,
                                   & length);
        RegCloseKey (key);

        if (result == ERROR_SUCCESS)
                return value;

        free (value);
        return 0;
}

/**
 * Retrieve a 64-bit value from the registry, defaulting to 0.
 */

ULONGLONG getValue (const wchar_t * path, const wchar_t * valueName) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_CURRENT_USER, path, 0, KEY_READ, & key);
        if (result != ERROR_SUCCESS)
                return 0;

        unsigned long   type;
        ULONGLONG       value = 0;
        unsigned long   length = sizeof (value);

        ULONGLONG       data;
        result = RegQueryValueExW (key, valueName, 0, & type, (BYTE *) & value,
                                   & length);
        RegCloseKey (key);

        if (result == ERROR_SUCCESS && length == sizeof (value))
                return value;

        return 0;
}

/**
 * Set the monitor process to autostart, or disable it.
 */

void setAutostart (bool state) {
        setSetting (WINDOWS_RUN, RUN_VALUE, state ? appPath : 0);
}

/**
 * Retrieve the current auto-start state.
 */

bool getAutoStart (void) {
        return testSetting (WINDOWS_RUN, RUN_VALUE);
}

/**
 * Set the enable state for the filter.
 */

void setFilter (bool state) {
        setSetting (LIMIT_SETTINGS, DISABLE_VALUE, state ? L"1" : 0);

        /*
         * Cause the filter setting to be applied to any existing filter
         * instance.
         */

        filterDisabled = state;
        steamProcess = 0;
}

/**
 * Get the current enable state for the filter.
 */

bool getFilter (void) {
        return testSetting (LIMIT_SETTINGS, DISABLE_VALUE);
}


/**
 * Use ShellExecute to fire off a URL, especially for the steam:// URL scheme.
 */

void runCommand (const wchar_t * command, const wchar_t * parameters = 0) {
        SHELLEXECUTEINFOW exec = { sizeof (exec) };
        exec.lpFile = command;
        exec.lpParameters = parameters;

        /*
         * Force the current directory to be the application directory.
         */

        wchar_t         path [1024];
        wcscpy_s (path, ARRAY_LENGTH (path), appPath);

        wchar_t       * end = wcsrchr (path, '\\');
        if (end == 0)
                return;
        * end = 0;

        exec.lpDirectory = path;
        ShellExecuteExW (& exec);
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

        if (now - upgradeCheckTime > UPGRADE_CHECK_DELTA) {
                upgradeCheckTime = now;
                setValue (LIMIT_SETTINGS, TIMESTAMP_VALUE, & now);
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

        if (attach && processId == steamProcess)
                return;
        if (! attach && steamProcess == 0)
                return;

        /*
         * Verify that the process ID actually belongs to steam.exe, and isn't
         * for example a Steam window opened by explorer.exe.
         */

        if (serverName != 0 && attach && ! filterDisabled) {
                if (steamProcess != 0)
                        callFilterId (steamProcess, "FilterUnload");

                if (! callFilterId (processId, "SteamFilter", serverName))
                        return;

                /*
                 * The injection succeeded.
                 */

                steamProcess = processId;
        } else {
                callFilterId (processId, "FilterUnload");
                steamProcess = 0;
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
                if (window == aboutWindow)
                        aboutWindow = 0;

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
 * Create and show the "About" window.
 */

void showAbout (void) {
        if (aboutWindow != 0) {
                SetFocus (aboutWindow);
                return;
        }

        wchar_t       * nextVersion;
        nextVersion = getSetting (LIMIT_SETTINGS, VERSION_VALUE);

        wchar_t       * dialog = MAKEINTRESOURCE (IDD_ABOUT);
        bool            upgrade = false;

        while (nextVersion != 0) {
                /*
                 * String version comparison is tricky; for now, as a hack make
                 * version strings of equal length compare lexically, but make
                 * a longer string also able to win (for long build numbers or
                 * the like).
                 */

                int             diff = wcscmp (nextVersion, appVer);
                if (diff == 0)
                        break;
                if (diff < 0 && wcslen (nextVersion) <= wcslen (appVer))
                        break;

                upgrade = true;
                dialog = MAKEINTRESOURCE (IDD_ABOUT_UPGRADE);
                break;
        }

        aboutWindow = CreateDialogW (GetModuleHandle (0), dialog, 0, aboutProc);
        if (aboutWindow == 0)
                return;

        wchar_t         text [1024];
        GetDlgItemTextW (aboutWindow, IDC_APPNAME, text, ARRAY_LENGTH (text));

        wcscat_s (text, ARRAY_LENGTH (text), L" ");
        wcscat_s (text, ARRAY_LENGTH (text), appVer);

        SetDlgItemTextW (aboutWindow, IDC_APPNAME, text);

        ShowWindow (aboutWindow, SW_SHOW);
}

/**
 * Dialog procedure for the server picker.
 */

INT_PTR CALLBACK pickProc (HWND window, UINT message, WPARAM wparam,
                           LPARAM lparam) {
        switch (message) {
        case WM_INITDIALOG: {
                /*
                 * Provide some text for the taskbar, since setting a caption
                 * in the dialog resource forces a window title.
                 */

                wchar_t         title [1024];
                LoadString (GetModuleHandle (0), IDS_PICKSERVER,
                            title, ARRAY_LENGTH (title));
                SetWindowText (window, title);
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

        if (code != BN_CLICKED)
                return FALSE;

        if (item != IDCANCEL) {
                HWND            combo = GetDlgItem (window, IDCB_PICKER);
                LRESULT         result;
                result = SendMessage (combo, CB_GETCURSEL, 0, 0);

                /*
                 * If there's no selection, use the text in the edit control
                 * as the IP or host address to actually filter. Otherwise map
                 * the selection index to a string resource with the IP.
                 */

                wchar_t         text [1024];
                if (result == CB_ERR) {
                        GetWindowText (combo, text, ARRAY_LENGTH (text));
                } else {
                        LoadStringW (GetModuleHandle (0), IDS_ADDRESS0 + result,
                                     text, ARRAY_LENGTH (text));
                }

                setSetting (LIMIT_SETTINGS, SERVER_VALUE, text);

                if (serverName != 0)
                        free (serverName);
                serverName = wcsdup (text);

                /*
                 * Force the filter setting to be reapplied to any existing
                 * active filter.
                 */

                steamProcess = 0;
        }

        DestroyWindow (window);
        if (window == pickWindow)
                pickWindow = 0;

        return FALSE;
}

/**
 * Show the server picker.
 */

void showPicker (void) {
        if (pickWindow != 0) {
                SetFocus (pickWindow);
                return;
        }

        HMODULE         self = GetModuleHandle (0);
        pickWindow = CreateDialogW (self, MAKEINTRESOURCE (IDD_SERVERPICKER),
                                    0, pickProc);

        if (pickWindow == 0)
                return;

        HWND            combo = GetDlgItem (pickWindow, IDCB_PICKER);

        /*
         * Add strings to the dialog's combo box
         */

        wchar_t         text [1024];
        unsigned long   id = IDS_SERVER0;
        unsigned long   defaultId = ~ 0UL;

        for (;; ++ id) {
                unsigned long   serverId = id - IDS_SERVER0 + IDS_ADDRESS0;
                int             length;
                length = LoadStringW (self, serverId,
                                      text, ARRAY_LENGTH (text));
                if (length == 0)
                        break;

                bool            isDefault = serverName != 0 &&
                                            wcscmp (text, serverName) == 0;

                length = LoadStringW (self, id, text, ARRAY_LENGTH (text));
                if (length == 0)
                        break;

                LRESULT         result;
                result = SendMessage (combo, CB_ADDSTRING, 0, (LPARAM) text);

                if (result == CB_ERR)
                        break;

                if (isDefault)
                        defaultId = result;
        }

        if (defaultId == ~ 0UL) {
                SendMessage (combo, WM_SETTEXT, 0, (LPARAM) serverName);
        } else
                SendMessage (combo, CB_SETCURSEL, defaultId, 0);

        ShowWindow (pickWindow, SW_SHOW);
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

                case ID_CONTEXT_SERVER:
                        showPicker ();
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

        appPath = (wchar_t *) malloc ((length + 1) * sizeof (wchar_t));
        wcscpy_s (appPath, length + 1, path);

        /*
         * While we're at it, extract the actual app version string.
         */

        unsigned long   handle = 0;
        unsigned long   size = GetFileVersionInfoSize (appPath, & handle);

        void          * mem = malloc (size);
        GetFileVersionInfoW (appPath, handle, size, mem);

        VS_FIXEDFILEINFO * info;
        UINT            infoLength = 0;
        VerQueryValueW (mem, L"\\", (void **) & info, & infoLength);

        unsigned long   major = info->dwFileVersionMS >> 16;
        unsigned long   minor = info->dwFileVersionMS & 0xFFFF;
        unsigned long   build = info->dwFileVersionLS >> 16;
        unsigned long   rev = info->dwFileVersionLS & 0xFFFF;

        wsprintfW (path, L"%d.%d.%d.%d", major, minor, build, rev);

        length = wcslen (path) + 1;
        appVer = (wchar_t *) malloc (length * sizeof (wchar_t));
        wcscpy_s (appVer, length, path);

        return appPath;
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

        upgradeCheckTime = getValue (LIMIT_SETTINGS, TIMESTAMP_VALUE);
        if (upgradeCheckTime == 0) {
                GetSystemTimeAsFileTime ((FILETIME *) & upgradeCheckTime);
                setValue (LIMIT_SETTINGS, TIMESTAMP_VALUE, & upgradeCheckTime);
        }

        /*
         * Get the server IP to filter from the registry, along with the last
         * state of the enable/disable flag.
         */

        wchar_t       * temp = getSetting (LIMIT_SETTINGS, SERVER_VALUE);
        if (temp != 0)
                serverName = temp;

        filterDisabled = getFilter ();

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

                        if (aboutWindow != 0 &&
                            IsDialogMessageW (aboutWindow, & message)) {
                                continue;
                        }

                        if (pickWindow != 0 &&
                            IsDialogMessageW (pickWindow, & message)) {
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
