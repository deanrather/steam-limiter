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
 * The prototype of the main function we're hooking is:
 *   int WSAAPI connect (SOCKET s, const struct sockaddr *name, int namelen);
 */

typedef int (WSAAPI * ConnectFunc) (SOCKET s, const sockaddr * name, int namelen);

/**
 * The prototype of the legacy sockets DNS name lookup API is:
 *   struct hostent * gethostbyname (const char * name);
 *
 * There are three functions that could be used for name resolution; this is
 * the original BSD Sockets one, and as the oldest it uses a fixed buffer for
 * the result that means no support at all for IPv6 is possible.
 *
 * The IETF-sanctioned replacement for this is getaddrinfo (), which is better
 * (although still problematic to use, being completely synchronous like all
 * the BSD socket APIs) and Microsoft have their own wide-character version for
 * applications which want to explicitly use Unicode or UTF-8 encodings. Older
 * applications written before IPv6 still tend to use gethostbyname (), so this
 * is normally the one to filter.
 */

typedef struct hostent * (WSAAPI * GetHostFunc) (const char * name);

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
 * This is the original connect () function, just past the patch area.
 */

ConnectFunc             g_connectResume;

/**
 * This is the original gethostbyname () function, just past the patch area.
 */

GetHostFunc             g_gethostResume;

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

int WSAAPI connectHook (SOCKET s, const sockaddr * name, int namelen) {
        sockaddr_in   * addr = (sockaddr_in *) name;

        /*
         * For now we just look for a single address and redirect connections
         * to it if the port matches. Making this use multiple addresses is
         * easy when allowing them to pass through, but more complex to use in
         * picking one to detour to.
         *
         * As there's no real substantial benefit to supporting more than one
         * at the moment (all New Zealand ISPs only use a single content server
         * in any case) supporting multiple filters is not really worth it.
         */

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
                
        return (* g_connectResume) (s, name, namelen);
}

/**
 * Ultra-simple glob matcher.
 */

bool globMatch (const char * example, const char * pattern) {
        if (example == 0)
                return false;

        char            ch;
        for (; (ch = * pattern) != 0 ; ++ pattern) {
                if (ch == '?') {
                        /*
                         * Allow any character or the end of the example string
                         * to match a '?'.
                         */

                        if (* example != 0)
                                ++ example;

                        continue;
                } else if (ch != '*') {
                        /*
                         * Exact match on one character.
                         */

                        if (* example != ch)
                                return false;

                        ++ example;
                        continue;
                }

                /*
                 * Wildcard match, consume any number of characters. As in the
                 * original UNIX v6 code, this is a fairly inefficient way of
                 * implementing Kleene-type closure compared to any of the more
                 * fun matching systems (Boyer-Moore, Knuth-Morris-Pratt, or
                 * most forms of regular expression execution via compilation
                 * to NFA/DFA/vm-code) but it's super-simple and as small code
                 * size is part of the design goal, why not?
                 */

                for (;;) {
                        /*
                         * Start with a recursive call just on the example, so
                         * that we permit 0 characters to match.
                         */

                        if (globMatch (example, pattern + 1))
                                return true;

                        if (* example == 0)
                                return false;

                        ++ example;
                }
        }
        return * example == 0;
}

/**
 * Hook for the gethostbyname () Sockets address-resolution function.
 *
 * This looks for a glob-type pattern to reject or accept the name resolution,
 * and for now just says that it can't find a host. At some point I should add
 * some function to configure the filter string, but for now it is what it is.
 */

struct hostent * WSAAPI gethostHook (const char * name) {
        if (! globMatch (name, "content*.steampowered.com"))
                return (* g_gethostResume) (name);

        OutputDebugStringA (name);
        OutputDebugStringA (" DNS result blocked\r\n");

        /*
         * On Windows, WSAGetLastError () and WSASetLastError () are
         * just thin wrappers around GetLastError ()/SetLastError (),
         * so we can use SetLastError ().
         *
         * Set up the right error number for a nonexistent host, since
         * this classic BSD sockets API doesn't have a proper error
         * reporting mechanism and that's what most code looks for to
         * see what went wrong.
         */

        SetLastError (WSAHOST_NOT_FOUND);
        return 0;
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

#define MOV_EDI_EDI     0xFF8B
#define JMP_SHORT_MINUS5 (0xF900 + JMP_SHORT)

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
 * Use the built-in Windows run-time patching system for core DLLs.
 */

bool attachHook (void * address, void * newTarget) {
        /*
         * Check for the initial MOV EDI, EDI two-byte NOP in the target
         * function, to signify the presence of a free patch area.
         *
         * We'll rely on the x86's support for unaligned access here, as we're
         * always going to be doing this on an x86 or something with equivalent
         * support for unaligned access.
         */

        unsigned char * data = (unsigned char *) address;
        if (* (unsigned short *) data != MOV_EDI_EDI) {
                OutputDebugStringA ("SteamFilter: connect not patchable\n");
                return false;
        }

        /*
         * Write a branch to the hook stub over the initial NOP of the target
         * function.
         */

        unsigned long   protect = 0;
        if (! VirtualProtect (data - 5, 7, PAGE_EXECUTE_READWRITE, & protect))
                return false;

        /*
         * Put the long jump to the detour first (in space which is reserved
         * for just this purpose in code compiled for patching), then put the
         * short branch to the long jump in the two-byte slot at the regular
         * function entry point.
         */

        data [- 5] = JMP_LONG;
        writeOffset (data - 4, (unsigned char *) newTarget - data);
        * (unsigned short *) data = JMP_SHORT_MINUS5;
        return true;
}

/**
 * Remove an attached hook, based on the address past the detour point.
 *
 * In case the target DLL is actually unloaded, the write to the detour point
 * is guarded with a Win32 SEH block to avoid problems with the writes.
 */

void unhook (void * resumeAddress) {
        if (resumeAddress == 0)
                return;

        unsigned char * dest = (unsigned char *) resumeAddress - 2;

        __try {
                * (unsigned short *) dest = MOV_EDI_EDI;
                memset (dest - 5, 0x90, 5);
        } __finally {
        }
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

        FARPROC         gethostFunc = GetProcAddress (ws2, "gethostbyname");
        if (gethostFunc == 0)
                return 0;

        g_addrFunc = (GetAddrInfoWFunc) addrFunc;
        g_freeFunc = (FreeAddrInfoWFunc) freeFunc;

        if (address != 0)
                setFilter (address);

        /*
         * Actually establish the diversion on the Windows Sockets connect ()
         * function.
         */

        g_connectResume = (ConnectFunc) ((char *) connFunc + 2);
        if (! attachHook (connFunc, connectHook)) {
                OutputDebugStringA ("SteamFilter: connect not patchable\n");
                return 0;
        }

        g_gethostResume = (GetHostFunc) ((char *) gethostFunc + 2);
        if (! attachHook (gethostFunc, gethostHook)) {
                /*
                 * Back out the connect hook we just attached.
                 */

                unhook (g_connectResume);
                g_connectResume = 0;

                OutputDebugStringA ("SteamFilter: gethostbyname not patchable\n");
                return 0;
        }

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
        if (g_connectResume == 0)
                return;

        unhook (g_connectResume);
        g_connectResume = 0;

        unhook (g_gethostResume);
        g_gethostResume = 0;

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
