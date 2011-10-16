/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * This DLL is injected into the Steam process to filter the set of hosts that
 * we will allow Steam to connect to for the purpose of downloading content.
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
 *
 * Although I did have an earlier version which tried to use vectored exception
 * handling for detouring the connect function, this version is simpler and is
 * built to use the built-in patching facilities in Windows DLLs, as per
 * http://blogs.msdn.com/b/oldnewthing/archive/2011/09/21/10214405.aspx
 */

#include "../nolocale.h"
#include <winsock2.h>

/**
 * For declaring exported callable functions from the injection shim.
 */

#define STEAMDLL(type)  extern "C" __declspec (dllexport) type __stdcall

/**
 * The prototype of the function we're hooking is:
 *   int WSAAPI connect (SOCKET s, const struct sockaddr *name, int namelen);
 */

typedef int (WSAAPI * ConnectFunc) (SOCKET S, const sockaddr * name, int namelen);

/**
 * This is an assistant function in WS2_32.DLL we can use to parse a string
 * address.
 *
 * This allows us to take an address from the controlling program passed as a
 * parameter and convert it into the address to monitor.
 */

typedef int (WSAAPI * GetAddrInfoWFunc) (const wchar_t * node,
                                         const wchar_t * service,
                                         const ADDRINFOW * hints,
                                         ADDRINFOW ** result);

/**
 * Companion to the above to free memory it allocates.
 */

typedef void (WSAAPI * FreeAddrInfoWFunc) (ADDRINFOW * mem);

/**
 * To make GetAddrInfoW () easier to call from explicit dynamic linking.
 */

GetAddrInfoWFunc        g_addrFunc;
FreeAddrInfoWFunc       g_freeFunc;

/**
 * This is the original connect function, just past the patch bytes.
 */

ConnectFunc     connectResume;

/**
 * Like htons () but as a macro.
 */

#define SWAP(x)         ((((x) & 0xFF) << 8) | ((x) >> 8))

/**
 * The Telstra IP address we're after in network byte order.
 */

unsigned char   want [4] = {
        203, 167, 129, 4
};

/**
 * Hook for the connect () function; check if we want to rework it, or just
 * continue on to the original.
 */

int __stdcall connectHook (SOCKET s, const sockaddr * name, int namelen) {
        sockaddr_in   * addr = (sockaddr_in *) name;
        if (namelen == sizeof (sockaddr_in) &&
            addr->sin_family == AF_INET &&
            addr->sin_port == SWAP (27030)) {
                /*
                 * It's a content server connection, give it a pass or fail.
                 */

                if (memcmp (& addr->sin_addr, want, 4) == 0) {
                        OutputDebugStringA ("Content server permitted!\n");
                } else {
                        /*
                         * If we want to fail it, it seems that Steam is more
                         * happy with us subtly redirecting it at only the
                         * server we want to permit rather than returning
                         * WSAECONNREFUSED, which seems to make it loop trying
                         * to find a second content server.
                         */

                        OutputDebugStringA ("Content server redirected!\n");
                        memcpy (& addr->sin_addr, want, 4);
                }
        }
                
        return (* connectResume) (s, name, namelen);
}

/**
 * Write a 32-bit value into the output in Intel byte order.
 */

unsigned char * writeOffset (unsigned char * dest, unsigned long value) {
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
 * Code-generation stuff.
 */

#define JMP_LONG        0xE9
#define JMP_SHORT       0xEB

/**
 * Record our HMODULE value for unloading.
 */

HMODULE         g_instance;

/**
 * Set up the address to direct the content server connections to.
 */

int setFilter (wchar_t * address) {
        if (g_addrFunc == 0 || g_freeFunc == 0 || address == 0)
                return 0;

        ADDRINFOW     * result;
        if ((* g_addrFunc) (address, L"27030", 0, & result) != 0)
                return 0;

        ADDRINFOW     * scan = result;
        while (scan != 0) {
                if (scan->ai_family == AF_INET) {
                        sockaddr_in   * inet = (sockaddr_in *) scan->ai_addr;
                        memcpy (& want, & inet->sin_addr, sizeof (want));
                        break;
                }

                scan = scan->ai_next;
        }

        OutputDebugStringW (address);
        OutputDebugStringA (scan == 0 ? " filter not resolved\n" :
                            " filter installed");

        (* g_freeFunc) (result);
        return 1;
}

/**
 * Establish the hook filter we want on the connect function in WS2_32.DLL
 */

STEAMDLL (int) SteamFilter (wchar_t * address) {
        /*
         * If we've already been called, this is a call to re-bind the address
         * being monitored.
         */

        if (g_addrFunc != 0)
                return setFilter (address);

        /*
         * Wait for the target module to be present, so as not to interfere
         * with any loading or initialization process in the host process.
         */

        HMODULE         ws2;
        for (;;) {
                ws2 = GetModuleHandle (L"WS2_32.DLL");
                if (ws2 != 0)
                        break;

                Sleep (1000);
        }

        /*
         * First find the function we're going to patch, and then while we're
         * there find the address of a utility function to turn a string host
         * name into a plain address.
         */

        FARPROC         connFunc = GetProcAddress (ws2, "connect");
        if (connFunc == 0)
                return 0;

        FARPROC         addrFunc = GetProcAddress (ws2, "GetAddrInfoW");
        if (addrFunc == 0)
                return 0;

        FARPROC         freeFunc = GetProcAddress (ws2, "FreeAddrInfoW");
        if (addrFunc == 0)
                return 0;

        g_addrFunc = (GetAddrInfoWFunc) addrFunc;
        g_freeFunc = (FreeAddrInfoWFunc) freeFunc;

        if (address != 0) {
                setFilter (address);
        }

        unsigned char * data = (unsigned char *) connFunc;
        if (data [0] != 0x8B && data [1] == 0xFF) {
                OutputDebugStringA ("SteamFilter: connect not patchable\n");
                return 0;
        }

        /*
        /*
         * Write a breakpoint instruction over the top of the target function.
         */

        unsigned long   protect = 0;
        if (! VirtualProtect (data - 5, 7, PAGE_EXECUTE_READWRITE, & protect))
                return 0;

        connectResume = (ConnectFunc) (data + 2);

        data [- 5] = JMP_LONG;
        writeOffset (data - 4, (unsigned char *) connectHook - data);
        * (unsigned short *) data = 0xF9EB;

        OutputDebugStringA ("SteamFilter hook attached\n");

        /*
         * Since we loaded OK, we want to stay loaded; add one to the refcount
         * for LoadLibrary; using GetModuleHandleEx () we can do this in one
         * easy call.
         */

        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR) SteamFilter,
                            & g_instance);

        return 1;
}

/**
 * Disable just the current hook.
 *
 * Do critical cleanup. Note that the Winsock DLL might actually have been
 * unloaded when we are called, so guard this with an exception handler in
 * case the hook address can't actually be written any more.
 */

void removeHook (void) {
        if (connectResume == 0)
                return;

        unsigned char * dest = (unsigned char *) connectResume - 2;

        __try {
                * (unsigned short *) dest = 0xFF8B;
                memset (dest - 5, 0x90, 5);
        } __finally {
                connectResume = 0;
        }

        OutputDebugStringA ("SteamFilter unhooked\n");
}

/**
 * Export an explicit unload entry point.
 *
 * This reduces the DLL LoadLibrary reference count by one to match the count
 * adjustment in SteamFilter () - the calling shim also holds a reference so
 * that this doesn't provoke an immediate unload, that happens in the caller.
 */

STEAMDLL (int) FilterUnload (void) {
        if (g_instance == 0)
                return 0;

        removeHook ();
        FreeLibrary (g_instance);
        g_instance = 0;
        return 1;
}

BOOL WINAPI DllMain (HINSTANCE instance, unsigned long reason, void *) {
        if (reason != DLL_PROCESS_DETACH)
                return TRUE;

        /*
         * Do critical cleanup. Note that the Winsock DLL might actually have
         * been unloaded when we are called, so we should guard this with an
         * exception handler in case the hook address can't actually be
         * written any more.
         */

        removeHook ();
}

/**@}*/
