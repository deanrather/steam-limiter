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

#include "../nolocale.h"

#include "resource.h"
#include <psapi.h>
#include <shellapi.h>
#include <stdlib.h>

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))
#define ARRAY_END(a)            ((a) + ARRAY_LENGTH (a))

#define WM_NOTIFYICON   (WM_USER + 1)

/**
 * Globally record the application's current path.
 */

wchar_t       * appPath;

/**
 * A string version of the application version.
 */

wchar_t       * appVer;

/**
 * The IP of the server we wish to limit Steam to using.
 */

wchar_t       * serverName;

/**
 * If filtering is (temporarily, anyway) currently disabled.
 */

bool            filterDisabled;

/**
 * The last known steam process ID we have worked with.
 *
 * This is stored mainly to stop repeated attempts to work on the same process
 * instance; setting it to 0 will cause the filter settings to be reapplied on
 * the next poll attempt.
 */

unsigned long   steamProcess;

/**
 * Code array containing the initial shim we'll use to bootstrap in our filter
 * DLL to the target process.
 */

unsigned char   codeBytes [1024];

/**
 * If our "about" window is visible, the window should be here.
 */

HWND            aboutWindow;

/**
 * If the server-picker window is visible, the window should be here.
 */

HWND            pickWindow;

/**
 * Write a 32-bit value into the output in Intel byte order.
 */

unsigned char * writePointer (unsigned char * dest, void * ptr) {
        unsigned long   value = (unsigned long) ptr;
        * dest ++ = value & 0xFF;
        value >>= 8;
        * dest ++ = value & 0xFF;
        value >>= 8;
        * dest ++ = value & 0xFF;
        value >>= 8;
        * dest ++ = value & 0xFF;
        return dest;
}

/**
 * Write a string into the output. The output should be aligned properly.
 */

unsigned char * writeString (unsigned char * dest, const wchar_t * src) {
        unsigned long   length = (wcslen (src) + 1) * sizeof (wchar_t);
        memcpy (dest, src, length);
        return dest + length;
}

/**
 * Write a string into the output.
 */

unsigned char * writeString (unsigned char * dest, const char * src) {
        unsigned long   length = strlen (src) + 1;
        memcpy (dest, src, length);
        return dest + length;
}

/**
 * Various code-building macros we use to make the shim
 */

#define PUSH_EAX        0x50
#define PUSH_EBX        0x53
#define JE              0x74

#define MOV_RM          0x89
#define ESI_OFFSET_EAX  0x46
#define EBP_ESP         0xE5
#define ESP_EBP         0xEC

#define MOV_REG         0x8B
#define EBX_ESI_OFFSET  0x5E

#define LEA             0x8D
#define ESI_MEM         0x35
#define RET             0xC3

#define INDIRECT        0xFF
#define PUSH_ESI_OFFSET 0x76
#define CALL_ESI_OFFSET 0x56

#define LEA_ESI_ADDRESS(p, x) do { \
                * p ++ = LEA; \
                * p ++ = ESI_MEM; \
                p = writePointer (p, x); \
        } while (0)

#define LEA_EBX_ESI_AT(p, o) do { \
                * p ++ = LEA; \
                * p ++ = EBX_ESI_OFFSET; \
                * p ++ = (unsigned char) o; \
        } while (0)

#define MOV_ESI_AT_EAX(p, o) do { \
                * p ++ = MOV_RM; \
                * p ++ = ESI_OFFSET_EAX; \
                * p ++ = (unsigned char) o; \
        } while (0)

#define MOV_EBP_ESP(p) do { \
                * p ++ = MOV_RM; \
                * p ++ = EBP_ESP; \
        } while (0)

#define MOV_ESP_EBP(p) do { \
                * p ++ = MOV_RM; \
                * p ++ = ESP_EBP; \
        } while (0)

#define MOV_EBX_ESI_AT(p, o) do { \
                * p ++ = MOV_REG; \
                * p ++ = EBX_ESI_OFFSET; \
                * p ++ = (unsigned char) o; \
        } while (0)

#define PUSH_ESI_AT(p, o) do { \
                * p ++ = INDIRECT; \
                * p ++ = PUSH_ESI_OFFSET; \
                * p ++ = (unsigned char) o; \
        } while (0)

#define CALL_ESI_SIZE   3
#define CALL_ESI_AT(p, o) do { \
                * p ++ = INDIRECT; \
                * p ++ = CALL_ESI_OFFSET; \
                * p ++ = (unsigned char) o; \
        } while (0)

#define OR_EAX_EAX(p) do { \
                * p ++ = 0x0B; \
                * p ++ = 0xC0; \
        } while (0)

#define JE_(p, o) do { \
                * p ++ = JE; \
                * p ++ = (unsigned char) o; \
        } while (0)

/**
 * Inject our filter DLL into the steam process.
 *
 * This is done using fairly normal DLL injection techniques. One of the few
 * things I don't do to keep this clean is try and adapt to different load
 * addresses of KERNEL32.DLL; at some point Windows may introduce a form of
 * address-space randomization which will even go that far, in which case I'd
 * have to work slightly harder to bootstrap the injected code, but for now the
 * classic techniques should do.
 */

void injectFilter (HANDLE steam, unsigned char * mem, const char * entryPoint,
                   wchar_t * param = 0) {
        /*
         * Form a full path name to our shim DLL based on our own executable
         * file name.
         */

        wchar_t         path [1024];
        wcscpy_s (path, ARRAY_LENGTH (path), appPath);

        wchar_t       * end = wcsrchr (path, '\\');
        if (end == 0)
                return;

        ++ end;

        if (wcscpy_s (end, ARRAY_END (path) - end, L"steamfilter.dll") != 0)
                return;

        /*
         * The first two entries in the codeBytes will be the addresses of the
         * LoadLibrary and GetProcAddress functions which will bootstrap in our
         * monitor DLL.
         *
         * The point of doing things this way is that it's the easiest way to
         * avoid having to use relative addressing for the calls into the
         * routines.
         *
         * Note that the assumption that these things will match in the source
         * and destination processes will fail severely if the Win2 Application
         * Verifier is used on us, as that'll revector GetProcAddress to the
         * verifier shim which won't exist in the Steam process.
         */

        unsigned char * dest = codeBytes;
        unsigned char * start;

        HMODULE         kernel = GetModuleHandleA ("KERNEL32.DLL");
        if (kernel == 0)
                return;

        /*
         * Reserve space for a pointer to the parameters to be passed to the
         * invoked function. This uses a pointer rather than the data so that
         * we can reasonably assure ourselves that everything fits within the
         * 128-byte short offset encoding we use below.
         */

        unsigned long   loadLibOffset = dest - codeBytes;
        FARPROC         loadLib;
        loadLib = GetProcAddress (kernel, "LoadLibraryW");
        dest = writePointer (dest, loadLib);

        unsigned long   gpaOffset = dest - codeBytes;
        FARPROC         gpa;
        gpa = GetProcAddress (kernel, "GetProcAddress");
        dest = writePointer (dest, gpa);

        unsigned long   freeOffset = dest - codeBytes;
        FARPROC         freeLib;
        freeLib = GetProcAddress (kernel, "FreeLibrary");
        dest = writePointer (dest, freeLib);

        unsigned long   libOffset = dest - codeBytes;
        dest = writePointer (dest, 0);

        unsigned long   callOffset = dest - codeBytes;
        dest = writePointer (dest, 0);

        unsigned long   paramOffset = dest - codeBytes;
        dest = writePointer (dest, 0);

        unsigned long   pathOffset = dest - codeBytes;
        dest = writePointer (dest, 0);

        unsigned long   entryOffset = dest - codeBytes;
        dest = writePointer (dest, 0);

        /*
         * Write the parameter string, and then backpatch a pointer to it
         * into the structure slot above.
         */

        if (param != 0) {
                dest = writeString (start = dest, param);
                writePointer (codeBytes + paramOffset,
                              (char *) mem + (start - codeBytes));
        }

        /*
         * Write the actual path string.
         */

        dest = writeString (start = dest, path);
        writePointer (codeBytes + pathOffset,
                      (char *) mem + (start - codeBytes));

        /*
         * Now write out the name of the entry point we want to use; this is
         * in plain ASCII because GetProcAddress works that way.
         */

        if (entryPoint == 0)
                entryPoint = "SteamFilter";

        dest = writeString (start = dest, entryPoint);
        writePointer (codeBytes + entryOffset,
                      (char *) mem + (start - codeBytes));

        dest += (dest - codeBytes) & 1;

        /*
         * Starting here we build the actual x86 loader code.
         */

        unsigned long   codeOffset = dest - codeBytes;

        /*
         * Obtain a base pointer to our code frame which we can use to access
         * the values we've stashed in it above.
         */

        LEA_ESI_ADDRESS (dest, mem);

        /*
         * myLib = LoadLibraryW (L"steamfilter.dll");
         */

        PUSH_ESI_AT (dest, pathOffset);
        CALL_ESI_AT (dest, loadLibOffset);

        MOV_ESI_AT_EAX (dest, libOffset);

        /*
         * myFunc = GetProcAddress (mylib, "SteamFilter");
         */

        PUSH_ESI_AT (dest, entryOffset);
        * dest ++ = PUSH_EAX;
        CALL_ESI_AT (dest, gpaOffset);

        /*
         * if (myFunc != 0)
         *   (* myFunc) (param);
         *
         * Note that we guard this by saving ESP in EBP so we're robust if the
         * calling convention or parameter count don't match. We pass the base
         * of our data block as a parameter to the called function.
         */

        MOV_EBP_ESP (dest);
        PUSH_ESI_AT (dest, paramOffset);
        MOV_ESI_AT_EAX (dest, callOffset);
        OR_EAX_EAX (dest);
        JE_ (dest, CALL_ESI_SIZE);
        CALL_ESI_AT (dest, callOffset);
        MOV_ESP_EBP (dest);

        /*
         * FreeLibrary (myLib);
         * return;
         */

        PUSH_ESI_AT (dest, libOffset);
        CALL_ESI_AT (dest, freeOffset);
        * dest ++ = RET;

        /*
         * Now write the shim into the target process.
         */

        unsigned long   wrote;
        if (! WriteProcessMemory (steam, mem, codeBytes, sizeof (codeBytes),
                                  & wrote)) {
                return;
        }

        /*
         * Having written the shim, run it!
         */

        unsigned long   entry = (unsigned long) mem + codeOffset;

        unsigned long   id;
        HANDLE          thread;
        thread = CreateRemoteThread (steam, 0, 0, (LPTHREAD_START_ROUTINE) entry,
                                     0, 0, & id);
        if (thread == 0)
                return;

        /*
         * Get the thread's result.
         */

        WaitForSingleObject (thread, INFINITE);

        unsigned long   result;
        GetExitCodeThread (thread, & result);
}

/**
 * Call into our DLL by injection.
 */

void callFilter (HANDLE steam, const char * entryPoint, wchar_t * param = 0) {
        /*
         * Allocate a page of VM inside the target process.
         *
         * The returned address is usable only in the context of the target,
         * but we need it to construct the shim code to load our DLL.
         */

        void          * mem;
        mem = VirtualAllocEx (steam, 0, sizeof (codeBytes), MEM_COMMIT,
                              PAGE_EXECUTE_READWRITE);
        if (mem == 0)
                return;

        injectFilter (steam, (unsigned char *) mem, entryPoint, param);

        /*
         * Always unload our shim; if we want to call another function in the
         * loaded DLL, we can just build another shim.
         */

        VirtualFreeEx (steam, mem, 0, MEM_RELEASE);
}

/**
 * Poll for Steam application instances.
 */

void steamPoll (bool attach) {
        /*
         * Look for a Steam window.
         */

static  HWND            steam;
        if (steam == 0) {
                steam = FindWindowExW (0, 0, 0, L"Steam");
                if (steam == 0)
                        return;
        }

        /*
         * See if the Window is still active or was created by the same process
         * as the last known one, if any.
         */

        unsigned long   processId = 0;
        GetWindowThreadProcessId (steam, & processId);

        if (attach && processId == steamProcess)
                return;

        steamProcess = processId;

        unsigned long   access;
        access = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                 PROCESS_VM_READ | PROCESS_VM_WRITE;

        HANDLE          proc;
        proc = OpenProcess (access, FALSE, steamProcess);
        if (proc == 0)
                return;

        if (serverName != 0 && attach && ! filterDisabled) {
                callFilter (proc, "SteamFilter", serverName);
        } else
                callFilter (proc, "FilterUnload");

        CloseHandle (proc);
}

/**
 * The standard Windows shell key for application launch at login.
 */

#define WINDOWS_RUN     L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE       L"SteamLimiter"

/**
 * Other related registry paths.
 */

#define LIMIT_SETTINGS  L"Software\\" RUN_VALUE
#define SERVER_VALUE    L"Server"
#define DISABLE_VALUE   L"Disabled"

/**
 * Common code for managing simple settings.
 */

void setSetting (const wchar_t * path, const wchar_t * valueName,
                 const wchar_t * value) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_LOCAL_MACHINE, path, 0, KEY_WRITE, & key);
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
 * Common code for testing a simple (boolean) setting.
 */

bool testSetting (const wchar_t * path, const wchar_t * valueName) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_LOCAL_MACHINE, path, 0, KEY_READ, & key);
        if (result != ERROR_SUCCESS)
                return false;

        unsigned long   type;
        unsigned long   length;
        result = RegQueryValueExW (key, valueName, 0, & type, 0, & length);

        return result == ERROR_SUCCESS && type == REG_SZ;
}

/**
 * Read a simple string setting.
 */

wchar_t * getSetting (const wchar_t * path, const wchar_t * valueName) {
        HKEY            key;
        LSTATUS         result;
        result = RegOpenKeyExW (HKEY_LOCAL_MACHINE, path, 0, KEY_READ, & key);
        if (result != ERROR_SUCCESS)
                return 0;

        unsigned long   type;
        unsigned long   length;
        result = RegQueryValueExW (key, valueName, 0, & type, 0, & length);
        if (result != ERROR_SUCCESS || type != REG_SZ)
                return 0;

        wchar_t       * value = (wchar_t *) malloc (length);
        result = RegQueryValueExW (key, valueName, 0, & type, (BYTE *) value,
                                   & length);

        if (result == ERROR_SUCCESS)
                return value;

        free (value);
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
 * Use ShellExecute to fire off a URL, especially for the steam:// URL scheme.
 */

void runCommand (const wchar_t * command) {
        SHELLEXECUTEINFOW exec = { sizeof (exec) };
        exec.lpFile = command;
        ShellExecuteEx (& exec);
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

        switch (message) {
        case WM_COMMAND:
                if (item != IDOK || code != BN_CLICKED)
                        break;

                DestroyWindow (window);
                if (window == aboutWindow)
                        aboutWindow = 0;
                return true;

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

        aboutWindow = CreateDialogW (GetModuleHandle (0),
                                     MAKEINTRESOURCE (IDD_ABOUT), 0, aboutProc);
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
        if (message != WM_COMMAND)
                return FALSE;

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
                PostQuitMessage (0);
                return 0;

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

                case ID_CONTEXT_SITE:
                        runCommand (L"http://steam-limiter.googlecode.com");
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

        wsprintfW (path, L"%d.%d.%d", major, minor, build);

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

        for (; __argc > 1 ; -- __argc, ++ __wargv) {
                if (wcscmp (__wargv [1], L"-quit") == 0)
                        quit = true;
        }

        /**
         * Look for an existing instance of the application.
         */

        HWND            window;
        window = FindWindowEx (0, 0, L"SteamMonitor", 0);

        if (window != 0) {
                /*
                 * If there is an existing instance, we always exit; we may,
                 * however, ask the existing instance to close, and for the
                 * benefit of the invoking process we can wait until the window
                 * no longer exists.
                 */

                while (quit) {
                        SendMessageW (window, WM_CLOSE, 0, 0);

                        window = FindWindowEx (0, 0, L"SteamMonitor", 0);
                        if (window == 0)
                                break;

                        Sleep (100);
                }
                return 0;
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
         * Get the server IP to filter from the registry, along with the last
         * state of the enable/disable flag.
         */

        wchar_t       * temp = getSetting (LIMIT_SETTINGS, SERVER_VALUE);
        if (temp != 0)
                serverName = temp;

        filterDisabled = getFilter ();

        for (;;) {
                unsigned long   wait;
                wait = MsgWaitForMultipleObjects (0, 0, 0, 1000, QS_ALLINPUT);

                if (wait == WAIT_TIMEOUT) {
                        steamPoll (true);
                        continue;
                }

                MSG             message;
                while (PeekMessage (& message, 0, 0, 0, TRUE)) {
                        if (message.message == WM_QUIT) {
                                Shell_NotifyIconW (NIM_DELETE, & data);
                                steamPoll (false);
                                return 0;
                        }

                        if (aboutWindow != 0 &&
                            IsDialogMessageW (aboutWindow, & message)) {
                                continue;
                        }

                        TranslateMessage (& message);
                        DispatchMessage (& message);
                }
        }

}


/**@}*/
