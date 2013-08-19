/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * This DLL is injected into the Steam process to filter the set of hosts that
 * we will allow Steam to connect to for the purpose of downloading content.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2011-2013 Nigel Bree; All Rights Reserved.
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

/*
 * Although I did have an earlier version which tried to use vectored exception
 * handling for detouring the connect function, this version is simpler and is
 * built to use the built-in patching facilities in Windows DLLs, as per
 * http://blogs.msdn.com/b/oldnewthing/archive/2011/09/21/10214405.aspx
 */

#include "../nolocale.h"
#include "../limitver.h"

#include <winsock2.h>

#include "glob.h"
#include "filterrule.h"
#include "replace.h"

/**
 * For declaring exported callable functions from the injection shim.
 */

#define STEAMDLL(type)  extern "C" __declspec (dllexport) type __stdcall

/**
 * Simple cliches for measuring arrays.
 */

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))

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
 * Prototype of the legacy sockets recv () function.
 */

typedef int   (WSAAPI * RecvFunc) (SOCKET s, char * buf, int len, int flags);

/**
 * Prototype of the legacy sockets recvfrom () function.
 */

typedef int   (WSAAPI * RecvFromFunc) (SOCKET s, char * buf, int len, int flags,
                                       sockaddr * from, int * fromLen);

/**
 * Prototype of the modern asynchronous WSARecv () function.
 */

typedef int   (WSAAPI * WSARecvFunc) (SOCKET s, LPWSABUF buffers,
                                      unsigned long count,
                                      unsigned long * received,
                                      unsigned long * flags,
                                      OVERLAPPED * overlapped,
                                      LPWSAOVERLAPPED_COMPLETION_ROUTINE handler);
/*
 * The prototype of the legacy sockets send () function. 
 */

typedef int   (WSAAPI * SendFunc) (SOCKET s, const char * buf, int len, int flags);

/*
 * Prototype of the modern asynchronous WSASend () function. 
 */

typedef int   (WSAAPI * WSASendFunc) (SOCKET s, LPWSABUF buffers,
                                      unsigned long count,
                                      unsigned long * sent, unsigned long flags,
                                      OVERLAPPED * overlapped,
                                      LPWSAOVERLAPPED_COMPLETION_ROUTINE handler);

/**
 * Prototype of WSAGetOverlappedResult (), used with WSASend () and WSARecv ().
 */

typedef BOOL  (WSAAPI * WSAGetOverlappedFunc) (SOCKET s, OVERLAPPED * overlapped,
                                               unsigned long * length, BOOL wait,
                                               unsigned long * flags);

/**
 * Prototype of the classic sockets select () function.
 *
 * This is a horribly obsolete approach; select () was bad API design for the
 * 80's and "modern" equivalents like epoll () are hardly an improvement (that
 * being on the few things that Dennis Ritchie's otherwise magisterial STREAMS
 * design didn't fix, poll () as the user-level interface to events in SVR4 not
 * being a good design; it took POSIX.4 and real-time signals to make any kind
 * of worthwhile AIO possible in UNIX and no vendor other than Sun bothered to
 * do a fully capable implementation).
 *
 * Stunningly, there is actually a piece of good design in Linux with respect
 * to AIO, which is signalfd (2). Unfortunately it's not available elsewhere
 * and even in Linux it doesn't work at all with sockets. However, it's the
 * closest thing to the UNIX philosophy and although the Sun people wisely used
 * Completion Ports from Windows as their model, signalfd () is probably the
 * single best idea out there (even better than BSD kqueues given how basic and
 * universally useful working AIO is to applications).
 *
 * For whatever reason, the HTTP code in Steam uses this instead of any of the
 * high-performance systems in Windows.
 */

typedef int   (WSAAPI * selectFunc) (int count, fd_set * read, fd_set * write,
                                     fd_set * error,
                                     const struct timeval * timeout);

/**
 * Prototype of WSAEventSelect () used with WSAEnumNetworkEvents ().
 *
 * This is something that needs to implement state-tracking for sockets to be
 * useful to wrap; the point in wrapping it is to be able to generate synthetic
 * read events, because otherwise if we want to emulate something on the read
 * side the calling application will generally be waiting on the event handle
 * before attempting to call WSARecv () due to the generally poor structure of
 * the eventing system in sockets as compared to true AIO as in Windows.
 */

typedef int   (WSAAPI * WSAEventSelectFunc) (SOCKET s, WSAEVENT event,
                                             long mask);

/**
 * Prototype of WSAEnumNetworkEvents (), used to detect socket status.
 */

typedef int   (WSAAPI * WSAEnumNetworkEventsFunc) (SOCKET s, WSAEVENT event,
                                                   WSANETWORKEVENTS * events);

/**
 * Prototype for getpeername (), which we don't hook but do want to call.
 */

typedef int   (WSAAPI * getpeernameFunc) (SOCKET s, sockaddr * addr, int * length);

/**
 * Prototype for closesocket (), to detect when to release tracking data.
 */

typedef int   (WSAAPI * closesocketFunc) (SOCKET s);

/**
 * Simple equivalent to ntohs.
 *
 * I don't want a static DLL dependency against ntohs () and this is easier
 * than making the dependency fully dynamic.
 */

#define ntohs(x)        ((unsigned char) ((x) >> 8) + \
                         ((unsigned char) (x) << 8))

/**
 * For representing hooked functions, and wrapping up the hook and unhook
 * machinery.
 *
 * Just a placeholder for now, but I intend to factor the hook machinery into
 * here and make it a separate file at some point, along with making the hook
 * attach and detach operations maintain state for easier unhooking.
 */

struct ApiHook {
        FARPROC         m_original;
        FARPROC         m_resume;
        FARPROC         m_hook;
        unsigned char   m_save [8];
        unsigned char   m_thunk [16];

                        ApiHook ();
                      ~ ApiHook ();

        bool            operator == (int) const { return m_resume == 0; }
        bool            operator != (int) const { return m_resume != 0; }

        FARPROC         makeThunk (unsigned char * data, size_t length);
        void            unhook (void);

        bool            attach (void * address, FARPROC hook);
        bool            attach (void * hook, HMODULE lib, const char * name);
};

/**
 * Specialize ApiHook for the function calling signatures.
 */

template <class F>
struct Hook : public ApiHook {
        F               operator * (void) {
                return (F) m_resume;
        }

        bool            attach (F hook, HMODULE lib, const char * name);
};

/**
 * Hook structures for all the things we want to intercept.
 * @{
 */

Hook<ConnectFunc>       g_connectHook;
Hook<GetHostFunc>       g_gethostHook;
Hook<RecvFunc>          g_recvHook;
Hook<RecvFromFunc>      g_recvfromHook;
Hook<WSARecvFunc>       g_wsaRecvHook;
Hook<selectFunc>        g_select_Hook;
Hook<SendFunc>          g_sendHook;
Hook<WSAGetOverlappedFunc> g_wsaGetOverlappedHook;
Hook<WSAEventSelectFunc> g_wsaEventSelectHook;
Hook<WSAEnumNetworkEventsFunc> g_wsaEnumNetworkEventsHook;
Hook<WSASendFunc>       g_wsaSendHook;
Hook<closesocketFunc>   g_closesocket_Hook;

getpeernameFunc         g_getpeername;

/**@}*/

/**
 * As a safety thing to prevent crashes on unload, set a global counter when
 * inside a hooked function - particularly the emulation of select (), since
 * that has a timeout; because of the timeout we could be inside the wrapped
 * version of the function in one thread while another thread is trying to
 * unload all the hook functions.
 */

class InHook {
public:
static  LONG            g_hookCount;

                        InHook () { InterlockedIncrement (& g_hookCount); }
                      ~ InHook () { InterlockedDecrement (& g_hookCount); }
};

/**
 * This holds all the rules we apply.
 */

FilterRules     g_rules (27030);

/**
 * Special-case passthrough until there's a DNS lookup.
 *
 * This seems to be necessary because of Steam Workshop; there's an auth step
 * in the port 27030 protocol and although it's pretty much useless these days
 * if this doesn't succeed when Steam first starts it's in a half-offline mode.
 * This doesn't have any negative effect, *except* it appears for Workshop
 * games; starting a Steam Workshop game seems to trigger some update process
 * that fails internally in Steam if it hasn't been able to do a login to a
 * set of servers on port 27030 at startup.
 *
 * So, I disable connect filter rules temporarily on first load until I see a
 * DNS query issue; since Steam normally does these fairly often this should
 * not affect the filter normally but it should let Steam do whatever it wants
 * for a few seconds when it's first loading and this seems to resolve any
 * quirk it has with Steam Workshop games.
 *
 * It's still an ugly hack, but probably the only other approach to deal with
 * this (unless I find an API that Steam is using to get the initial address
 * Steam connects to, because it's not gethostbyname - it seems to be something
 * cached somewhere) would be to get into the business of walking the stack or
 * inspecting thread IDs to discriminate between this case and downloads. But
 * in reality that's not going to be particularly robust either, and I don't
 * want to wander anywhere near the grey area of peeking at the actual Steam
 * client code with a disassembler to figure out an alternative.
 */

bool            g_passthrough = true;

/**
 * Hook for the connect () function; check if we want to rework it, or just
 * continue on to the original.
 */

int WSAAPI connectHook (SOCKET s, const sockaddr * name, int namelen) {
        InHook          hooking;

        /*
         * Capture the caller's return address so we can map it to a module and
         * thus a module name, to potentially include in the filter string.
         *
         * An alternative to this is to use RtlCaptureStackBacktrace to get the
         * caller (and potentially more of the stack), but since we're x86 only
         * for now this should do fine. Doing a stack backtrace along with a
         * Bloom filter is an interesting way to categorize events and with a
         * webservice could be pretty robust, but it's more complexity than I
         * probably want here.
         */

        HMODULE         module = 0;

#if     0
        unsigned long   addr = ((unsigned long *) & s) [- 1];
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR) addr, & module);
#endif

        const sockaddr_in * old = (const sockaddr_in *) name;
        sockaddr_in   * replace = 0;
        
        if (g_passthrough || name->sa_family != AF_INET ||
            ! g_rules.matchIp (old, module, & replace)) {
                /*
                 * Just forward on to the original. The 'passthrough' case is
                 * for when Steam starts up and does an auth interchange over
                 * port 27030 that many ISP-specific Steam servers can't handle
                 * (specifically the TelstraClear one fails this, which can
                 * lead Steam Workshop games like Skyrim to not load later on).
                 *
                 * So, I temporarily let things pass until I see a DNS query,
                 * just to handle Steam's strange start-up behaviour. Probably
                 * going offline and online within Steam will do the same thing
                 * but restarting the Steam client will be way more common and
                 * I want the 99% case to work.
                 */

                if (g_passthrough)
                        OutputDebugStringA ("passthrough\r\n");

                return (* g_connectHook) (s, name, namelen);
        }

        /*
         * If no replacement is specified, deny the connection. This isn't used
         * for Steam blocking in most cases because it responds to this by just
         * trying another server entirely.
         */

        if (replace == 0 || replace->sin_addr.S_un.S_addr == INADDR_NONE) {
                OutputDebugStringA ("Connect refused\r\n");
                SetLastError (WSAECONNREFUSED);
                return SOCKET_ERROR;
        }

        /*
         * Redirect the connection; put the rewritten address into a temporary
         * so the change isn't visible to the caller (Steam doesn't appear to
         * care either way, but it's best to be careful).
         */

        sockaddr_in   * base = (sockaddr_in *) name;
        sockaddr_in     temp;
        temp.sin_family = base->sin_family;
        temp.sin_port = replace->sin_port != 0 ? replace->sin_port :
                        base->sin_port;
        temp.sin_addr = replace->sin_addr.S_un.S_addr != 0 ?
                        replace->sin_addr : base->sin_addr;

        /*
         * Describe the redirection for the benefit of DbgView
         */

        char            show [128];
        unsigned char * bytes = & temp.sin_addr.S_un.S_un_b.s_b1;
        const unsigned char * prev = & old->sin_addr.S_un.S_un_b.s_b1;
        wsprintfA (show, "Connect redirected %d.%d.%d.%d to %d.%d.%d.%d:%d\r\n",
                   prev [0], prev [1], prev [2], prev [3],
                   bytes [0], bytes [1], bytes [2], bytes [3],
                   ntohs (temp.sin_port));

        OutputDebugStringA (show);
                
        return (* g_connectHook) (s, (sockaddr *) & temp, sizeof (temp));
}

/**
 * Hook for the gethostbyname () Sockets address-resolution function.
 */

struct hostent * WSAAPI gethostHook (const char * name) {
        InHook          hooking;

        /*
         * Disable the temporary pass-through mode once we see a DNS query.
         */

        g_passthrough = false;

        sockaddr_in   * replace = 0;

        if (! g_rules.matchDns (name, & replace)) {
                /*
                 * No matching rule, silently forward onward.
                 */

                return (* g_gethostHook) (name);
        }

        if (replace == 0 || replace->sin_addr.S_un.S_addr == INADDR_NONE) {
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

                char            show [128];
                wsprintfA (show, "lookup %.50s refused\r\n", name);
                OutputDebugStringA (show);
                SetLastError (WSAHOST_NOT_FOUND);
                return 0;
        }

        /*
         * Replacing a DNS result raises the question of storage, which for
         * base Windows sockets is per-thread; we could also choose to use a
         * non-thread-safe global like most UNIX implementations.
         *
         * One obvious trick would be to let the underlying call return the
         * structure which we modify, but the tradeoff there is that if it
         * fails we don't have a pointer.
         *
         * So for now, cheese out and use a global; also, copy the address
         * rather than point at the replacement.
         */

        hostent       * result = 0;
        if (replace != 0 && replace->sin_addr.S_un.S_addr == INADDR_ANY) {
                /*
                 * If the matching rule is a passthrough, then wrap it but do
                 * still log the result.
                 */

                result = (* g_gethostHook) (name);
        } else {
static  hostent         storage;
static  unsigned long   addr;
static  unsigned long * addrList [2] = { & addr };

                addr = replace->sin_addr.S_un.S_addr;
                storage.h_addrtype = AF_INET;
                storage.h_addr_list = (char **) addrList;
                storage.h_aliases = 0;
                storage.h_length = sizeof (addr);
                storage.h_name = "remapped.local";
                result = & storage;
        }

        char            show [128];
        if (result == 0) {
                wsprintfA (show, "lookup %.50s failed\r\n", name);
                OutputDebugStringA (show);
                return 0;
        }

        unsigned char * bytes = (unsigned char *) * result->h_addr_list;
        wsprintfA (show, "lookup %.50s as %d.%d.%d.%d\r\n",
                   name, bytes [0], bytes [1], bytes [2], bytes [3]);
        OutputDebugStringA (show);

        return result;
}

/**
 * For measuring bandwidth.
 */

class Meter {
public:
typedef CRITICAL_SECTION        Mutex;

private:
        Mutex           m_lock;
        unsigned long   m_now;
        unsigned long   m_currentBytes;

        unsigned long   m_last;
        long long       m_total;

        void            newTick (unsigned long now);

public:
                        Meter ();

        void            operator += (int bytes);
};

Meter :: Meter () : m_now (GetTickCount ()), m_currentBytes (0),
                m_last (0), m_total (0) {
        InitializeCriticalSection (& m_lock);
}

void Meter :: newTick (unsigned long now) {
        unsigned long   delta = now - m_now;
        if (delta < 1)
                return;

        unsigned long   bytes = m_currentBytes;
        m_currentBytes = 0;

        m_total += bytes;

        unsigned long   delta2 = m_now - m_last;

#if     0
        char            buf [80];
        wsprintfA (buf, "%d %d %d %X\r\n",
                   delta, bytes, delta2,
                   (unsigned long) m_total);
        OutputDebugStringA (buf);
#endif

        m_last = m_now;
        m_now = now;
}

void Meter :: operator += (int bytes) {
        if (bytes == SOCKET_ERROR)
                bytes = 0;

        EnterCriticalSection (& m_lock);

        unsigned long   now = GetTickCount ();
        newTick (now);

        m_currentBytes += bytes;

        LeaveCriticalSection (& m_lock);
}

Meter           g_meter;

/**
 * Hook the recv () API, to measure received bandwidth.
 *
 * Most of the time the underlying socket will probably be in non-blocking mode
 * as any use of this API will be from old code which has had to struggle with
 * the broken original UNIX sockets API design.
 */

int WSAAPI recvHook (SOCKET s, char * buf, int len, int flags) {
        InHook          hooking;

        Replacement   * replace;
        replace = g_findReplacement (s);
        if (replace != 0) {
                OutputDebugStringA ("Substituting HTTP response\r\n");

                unsigned long   count = 0;
                bool            ok;
                ok = g_consumeReplacement (replace, len, buf, & count);
                unsigned long   result = ok ? ERROR_SUCCESS : WSAEINVAL;
                SetLastError (result);

                return ok ? count : - 1;
        }

        int             result;
        result = (* g_recvHook) (s, buf, len, flags);
        g_meter += result;
        return result;
}

int WSAAPI recvfromHook (SOCKET s, char * buf, int len, int flags,
                         sockaddr * from, int * fromLen) {
        InHook          hooking;

        int             result;
        result = (* g_recvfromHook) (s, buf, len, flags, from, fromLen);
        g_meter += result;
        return result;
}

/**
 * Hook the WSARecv () API, to measure received bandwidth.
 *
 * This is a more interesting case because of the OVERLAPPED option; real, true
 * asynchronous I/O, and the traditional Steam CDN download does use it. Even
 * with AIO, there are several wrinkles thanks to the best part of AIO under
 * Windows, which is completion ports (which Steam doesn't use, although lots
 * of the serious code we might want to apply this to in future does).
 *
 * To deal with capturing overlapped completions fully, we need to also hook
 * WSAGetOverlappedResult () and probably WSAWaitForMultipleObjects (), which
 * will give us the option of slicing the end-user's original I/O up using a
 * custom OVERLAPPED buffer of our own in the slices (useful when we get to
 * trying to apply a bandwidth limit).
 */

int WSAAPI wsaRecvHook (SOCKET s, LPWSABUF buffers, unsigned long bytes,
                        unsigned long * received, unsigned long * flags,
                        OVERLAPPED * overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE handler) {
        InHook          hooking;

        Replacement   * replace;
        replace = g_findReplacement (s);
        if (replace != 0) {
                OutputDebugStringA ("Substituting HTTP response\r\n");

                unsigned long   count = 0;
                bool            ok;
                ok = g_consumeReplacement (replace, buffers->len, buffers->buf,
                                           & count);
                unsigned long   result = ok ? ERROR_SUCCESS : WSAEINVAL;
                SetLastError (result);

                if (overlapped != 0) {
                        overlapped->Internal = result;
                        overlapped->InternalHigh = count;

                        HANDLE          event = overlapped->hEvent;
                        event = (HANDLE) ((unsigned long) event & ~ 1);
                        if (event != 0)
                                SetEvent (event);
                }

                if (received != 0)
                        * received = count;

                if (handler != 0)
                        (* handler) (result, count, overlapped, 0);

                /*
                 * For now I don't support MSG_PEEK or MSG_WAITALL in the flags.
                 */

                return result;
        }

        if (overlapped != 0 || handler != 0) {
                int             result;
                result = (* g_wsaRecvHook) (s, buffers, bytes, received, flags,
                                              overlapped, handler);

                if (result == 0 && overlapped != 0) {
                        /**
                         * Synchronous success, process here.
                         */

                        g_meter += overlapped->InternalHigh;
                }
                return result;
        }

        bool            ignore;
        ignore = flags != 0 && (* flags & MSG_PEEK) != 0;

        int             result;
        result = (* g_wsaRecvHook) (s, buffers, bytes, received, flags,
                                    overlapped, handler);
        if (result != SOCKET_ERROR && ! ignore)
                g_meter += * received;
        return result;
}

/**
 * Hook for event object registration.
 */

int WSAAPI wsaEventSelectHook (SOCKET s, WSAEVENT event, long mask) {
        InHook          hooking;

        g_addEventHandle (s, event);

        return (* g_wsaEventSelectHook) (s, event, mask);
}

/**
 * Hook for socket close, mainly to handle event deregistration in our tracking
 * data.
 */

int WSAAPI closesocket_Hook (SOCKET s) {
        InHook          hooking;

        g_removeTracking (s);

        return (* g_closesocket_Hook) (s);
}

/**
 * Hook for event processing.
 *
 * Used if we want to simulate input.
 */

int WSAAPI wsaEnumNetworkEventsHook (SOCKET s, HANDLE event, WSANETWORKEVENTS * events) {
        InHook          hooking;

        int             result;
        result = (* g_wsaEnumNetworkEventsHook) (s, event, events);

        Replacement   * replace;
        replace = g_findReplacement (s);
        if (replace != 0)
                events->lNetworkEvents |= FD_READ;

        return result;
}

/**
 * Hook for event process, obsolete style.
 *
 * This is necessary if we want to simulate input events.
 *
 * The Windows Sockets version of the fd_set structure isn't a bitmap as in
 * the classic and hopelessly misdesigned BSD original, thankfully.
 */

int WSAAPI select_Hook (int count, fd_set * read, fd_set * write,
                        fd_set * error, const struct timeval * timeout) {
        InHook          hooking;

        /*
         * Look in the read set to see if there's something there we want to
         * trigger an event on. If there is, synthesize one immediately and
         * don't pass on to the inner Win32 implementation at all.
         */

        fd_set          result [1];
        FD_ZERO (result);

        int             readCount = read == 0 ? 0 : read->fd_count;
        int             i;
        for (i = 0 ; i < readCount ; ++ i) {
                SOCKET          s = read->fd_array [i];
                Replacement   * replace = g_findReplacement (s);

                if (replace != 0)
                        FD_SET (s, result);
        }

        if (result->fd_count > 0) {
                if (write != 0)
                        FD_ZERO (write);
                if (error != 0)
                        FD_ZERO (error);

                return result->fd_count;
        }

        /*
         * Pass through to the underlying implementation.
         *
         * This emulation isn't ideal, since if we're doing a select on one
         * thread and a second thread creates a condition we'd like to treat as
         * an emulated read, we don't detect that.
         *
         * However, generally applications that are thread-aware don't use any
         * of the really broken obsolete API design from UNIX sockets such as
         * select (). In practice, hopefully this limitation won't be a problem
         * and any properly threaded client application will use something that
         * is event-based.
         */

        return (* g_select_Hook) (count, read, write, error, timeout);
}

/**
 * Experimental hook for WSAGetOverlapped () in case we find a client doing
 * high-performance I/O through it.
 */

BOOL WSAAPI wsaGetOverlappedHook (SOCKET s, OVERLAPPED * overlapped,
                                  unsigned long * length, BOOL wait,
                                  unsigned long * flags) {
        InHook          hooking;

        BOOL            result;
        result = (* g_wsaGetOverlappedHook) (s, overlapped, length, wait, flags);

        return result;
}

/**
 * Sort-of equivalent to memicmp () for comparing HTTP header strings.
 *
 * Not using actual memicmp () just because of the broken locale machinery in
 * the VC++ RTL; this only cares about the C locale (i.e., ASCII).
 */

int c_memicmp (const void * leftBuf, const void * rightBuf, size_t length) {
typedef const unsigned char   * Str;
        Str             left = (Str) leftBuf;
        Str             right = (Str) rightBuf;

        while (length > 0) {
                unsigned char   lChar = * left;
                if (lChar > 'a' && lChar <= 'z')
                        lChar -= 32;
                unsigned char   rChar = * right;
                if (rChar > 'a' && rChar <= 'z')
                        rChar -= 32;

                if (lChar != rChar)
                        return lChar < rChar ? - 1 : 1;

                ++ left;
                ++ right;
                -- length;
        }

        return 0;
}

/**
 * Sort-of equivalent to strstr () for a HTTP header buffer.
 */

const char * c_memifind (const char * buf, const char * find, size_t length) {
        size_t          need = strlen (find);
        while (length >= need) {
                if (c_memicmp (buf, find, need) == 0)
                        return buf + need;

                /*
                 * Just slide one; fancier string matching algorithms like KMP
                 * or Boyer-Moore get their acceleration by doing clever things
                 * particularly with sliding (including matching from the
                 * rightmost edge of the pattern first, so they can slide the
                 * furthest amount).
                 */

                -- length;
                ++ buf;
        }

        return 0;
}

/**
 * Helper for filterHttpUrl () in cases where I want to actually rewrite a URL
 * (or more likely a host: header), splicing a replacement string over an old
 * one.
 *
 * Take an incoming buffer with a region in the middle to splice and take a
 * copy with the bit in the middle replaced.
 */

char * splice (const char * base, const char * from, const char * to,
               size_t & length, const char * replace, const char * concat = "") {
        size_t          first = from - base;
        size_t          subst = strlen (replace);
        size_t          subst2 = concat == 0 ? 0 : strlen (concat);
        size_t          rest = base + length - to;
        size_t          total = first + subst + subst2 + rest;
        char          * copy = (char *) HeapAlloc (GetProcessHeap (), 0, total + 1);

        char          * out = copy;
        if (first > 0) {
                memcpy (out, base, first);
                out += first;
        }
        if (subst > 0) {
                memcpy (out, replace, subst);
                out += subst;
        }
        if (subst2 > 0) {
                memcpy (out, concat, subst2);
                out += subst2;
        }
        if (rest > 0) {
                memcpy (out, to, rest);
                out += rest;
        }

        * out = 0;

#if     0
        OutputDebugStringA (out);
#endif
        return copy;
}


/**
 * Apply URL filters.
 *
 * The return value here is generally one of: length == 0 and result == 0
 * implies silent success, length > 0 and result == 0 implies an error should
 * be returned. Other results indicate that the returned buffer should be sent,
 * whether it's the caller's original or not.
 */

const char * filterHttpUrl (SOCKET s, const char * buf, size_t & length) {
        if (length < 10)
                return buf;

        /*
         * Match the verb text first, so we can only hook GET and POST.
         */

        size_t          verb = 0;
        if (c_memicmp (buf, "GET /", 5) == 0) {
                verb = 4;
        } else if (c_memicmp (buf, "POST /", 6) == 0)
                verb = 5;

        if (verb == 0)
                return buf;

        /*
         * Measure the URL before extracting a temporary copy for the filter
         * process.
         */

        const char    * end = (const char *) memchr (buf + verb, ' ', length);
        size_t          tempLen = end == 0 ? 0 : end - buf;
        char            temp [256];

        if (tempLen == 0)
                return buf;

        /*
         * To make filters more selective, find the host: header if supplied.
         *
         * Header parsing in HTTP is like header parsing in SMTP, and that is
         * way more complex than I want to bother with here. Since I'm just
         * faking out some simple hand-writter HTTP client code in STEAM rather
         * than doing a full filter, just look for the simple case. In reality,
         * for security purposes against a hostile target you do need to go the
         * whole way with a spec-compliant parser.
         */

        const char    * host = c_memifind (buf + tempLen, "host: ", length - tempLen);
        size_t          hostLength = 0;
        if (host != 0) {
                const char    * hostEnd;
                hostEnd = (const char *) memchr (host, '\r', buf + length - host);
                if (hostEnd != 0)
                        hostLength = hostEnd - host + 2;
        }

        char          * dest = temp;

        /*
         * Use getPeerName () so I can show the actual target IP in the debug
         * output, to contrast against the Host: value.
         */

        sockaddr_storage addr;
        int             addrLen = sizeof (addr);
        if (g_getpeername (s, (sockaddr *) & addr, & addrLen) == 0 &&
            addr.ss_family == AF_INET) {
                sockaddr_in   & in4 = (sockaddr_in &) addr;
                unsigned char * bytes = & in4.sin_addr.S_un.S_un_b.s_b1;
                wsprintfA (dest, "%d.%d.%d.%d:%d ",
                           bytes [0], bytes [1], bytes [2], bytes [3],
                           ntohs (in4.sin_port));

                dest += strlen (dest);
        }

        /*
         * If the requested URL is excessively large, truncate it (it gets big
         * because of useless query parameters Valve attach for debug/tracking,
         * that don't factor into what we care about).
         */

        size_t          avail = temp + sizeof (temp) - dest;

        if (tempLen + hostLength + 3 > avail) {
                if (hostLength + 3 > avail)
                        return buf;

                tempLen = avail - hostLength - 3;
        }

        memcpy (dest, buf, verb);
        dest += verb;

        bool            matchHost = false;
        const char    * newHost = 0;
        const char    * hostPart = 0;

        if (hostLength > 0) {
                /*
                 * Extract a copy of the host name, originally for the purpose
                 * of just printing it but now for matching it as well, using
                 * the '//' sigil at the front of the pattern (similar to how
                 * the URL patterns use a '/' sigil).
                 */

                hostPart = dest;
                dest [1] = dest [0] = '/';
                memcpy (dest + 2, host, hostLength - 2);
                dest [hostLength] = 0;

                matchHost = g_rules.matchHost (dest, & newHost);

                /*
                 * Now format the temp copy for printing and having the request
                 * URL glued to it.
                 */

                dest += hostLength;
        }

        const char    * urlPart = dest;
        memcpy (dest, buf + verb, tempLen - verb);
        dest += tempLen - verb;

        strcpy (dest, "\r\n");

        OutputDebugStringA (temp);
        * dest = 0;

        /*
         * Before we do the URL processing, perform a host replacement if it is
         * indicated; since that leaves the URL part in the same offset in the
         * copied buffer, it is easier to replace first before matching the URL.
         */

        while (matchHost) {
                /*
                 * An empty host means block.
                 */

                if (newHost == 0 || * newHost == 0) {
                        OutputDebugStringA ("Rejected host\r\n");
                        return 0;
                }

                while (* newHost == '/')
                        ++ newHost;

                /*
                 * A pattern of '*' means pass through (in this case to the
                 * URL processing.
                 */

                if (newHost [0] == '*' && newHost [1] == 0) {
                        newHost = 0;
                        break;
                }

                /*
                 * Replace the host: part with the replacement string and then
                 * continue with the URL matching.
                 */

                buf = splice (buf, host, host + hostLength, length,
                              newHost, "\r\n");

                OutputDebugStringA ("Replaced host\r\n");
                break;
        }

        /*
         * Now run the match against the URL (in the copy of the URL in the
         * stack).
         */

        const char    * replace = 0;
        if (! g_rules.matchUrl (urlPart, & replace)) {
                if (matchHost || hostPart == 0)
                        return buf;

                /*
                 * As a final attempt to decide, we can match the host+URL as a
                 * pair, although here all we do is succeed or fail - don't try
                 * to handle replacing.
                 *
                 * The aim here is to allow a pattern which matches the prefix
                 * of the URL to have a catch-all pattern for certain URLs that
                 * applies **if and only if** it hasn't been permitted by an
                 * earlier positive match.
                 */

                if (! g_rules.matchHost (hostPart, & newHost))
                        return buf;

                /*
                 * Pass it or fail it?
                 */

                if (newHost == 0 || * newHost == 0) {
                        OutputDebugStringA ("Rejected host+url\r\n");
                        return 0;
                }

                return buf;
        }

        /*
         * An empty pattern means block.
         */

        if (replace == 0 || * replace == 0) {
                OutputDebugStringA ("Rejected URL\r\n");
                return 0;
        }

        /*
         * A pattern starting with < means to provide a substitute reponse on
         * behalf of the connected server, so we filter the request and a later
         * receive call has the alternate content supplied.
         *
         * In practice this hasn't been as useful as I hoped, because the main
         * thing I want to replace is the /serverlist document but Steam caches
         * that excessively (making it hard to enable/disable at will because
         * of the need to manually restart Steam).
         *
         * However, I've also been submarined by an ISP-hosted CDN that is set
         * up with a virtual host rule that requires its own name rather than
         * the *.steampowered.com one. This forces me to have to look at rules
         * to process the hostnames and allow/disable hosts at that level.
         */

        if (replace [0] == '<') {
                /*
                 * Set the returned length to 0 if we sucessfully set up a new
                 * replacement document so that the caller doesn't pass the
                 * data through.
                 */

                if (g_addReplacement (s, replace + 1, urlPart))
                        length = 0;
                        
                return 0;
        }

        /*
         * A pattern of * or /* means pass through.
         *
         * To make this more robust, I'll skip over any leading '/' in the
         * replacement text, and I'll extend the length of the "verb" part to
         * including the '/' which has to be in the base request so that the
         * replacement process always produces a well-formed anchored URL.
         */

        ++ verb;
        while (replace [0] == '/')
                ++ replace;

        if (replace [0] == '*' && replace [1] == 0)
                return buf;

        /*
         * OK, what about more exotic cases? In principle I can replace the
         * original data block with our URL in place of the original.
         */

        char          * copy;
        copy = splice (buf, buf + verb, buf + tempLen, length, replace);

        /*
         * If we already built a temporary request due to replacing the host as
         * well as replacing the URL, free the buffer for the host copy.
         */

        if (newHost != 0)
                HeapFree (GetProcessHeap (), 0, (LPVOID) buf);

        return copy;

}

/**
 * Helper for WSASend () etc to optionally log raw sends to OutputDebugString.
 *
 * Since the output function requires a NUL terminator, break up the output
 * into segments (copying into a staging buffer).
 */

void debugWrite (const char * func, const char * buf, size_t len) {
        char            temp [128];
        wsprintfA (temp, "%s: %d bytes\r\n", func, len);
        OutputDebugStringA (temp);

        while (len > 0) {
                size_t          some = ARRAY_LENGTH (temp) - 1;
                if (len < some)
                        some = len;

                memcpy (temp, buf, some);
                temp [some] = 0;

                OutputDebugStringA (temp);

                buf += some;
                len -= some;
        }
}

/**
 * Flag whether to write raw data to OutputDebugString ().
 *
 * This is mainly for debugging; if needed I'll create a registry setting to
 * let people enable it in the field.
 */

bool            g_debugSend = false;

/**
 * Hook the legacy BSD sockets Send () function.
 *
 * One of the Steam beta clients uses the BSD functions instead of the native
 * Windows ones.
 */

int WSAAPI sendHook (SOCKET s, const char * buf, int len, int flags) {
        InHook          hooking;

        if (g_debugSend)
                debugWrite ("WSASend", buf, len);

        size_t          length = len;
        const char    * replace = buf;
        replace = filterHttpUrl (s, replace, length);

        if (length == 0) {
                OutputDebugStringA ("Substituting HTTP request\r\n");
                return 0;
        }

        if (buf == 0) {
                SetLastError (WSAECONNRESET);
                return SOCKET_ERROR;
        }

        /*
         * Pass-through is the simple case.
         */

        if (replace == buf)
                return (* g_sendHook) (s, buf, len, flags);

        /*
         * The complexity with replacing is mainly in the return value to hide
         * the extra length we inserted.
         */

        int             result;
        result = (* g_sendHook) (s, replace, (int) length, flags);

        HeapFree (GetProcessHeap (), 0, (LPVOID) replace);

        if (result == length)
                return len;

        return 0;
}

/**
 * Hook WSASend and do some basic inspection of the outgoing data.
 *
 * The intention here is to allow some crude filtering of HTTP URLs, in a form
 * that is lower-impact than full proxying. Since like most apps that embed an
 * HTTP client the entire request is sent in a single write call to the network
 * stack, we can extract and inspect the requested URL pretty simply.
 *
 * Now, in the most general case we'd also want to do connection tracking so we
 * only really worry about this when it's the first thing sent on a socket. But
 * since the amount of originated traffic is so small here, we can afford to
 * just look for the HTTP verbs on every write for now.
 */

int WSAAPI wsaSendHook (SOCKET s, LPWSABUF buffers, unsigned long count,
                        unsigned long * sent, unsigned long flags,
                        OVERLAPPED * overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE handler) {
        InHook          hooking;

        const char    * buf = buffers [0].buf;
        size_t          len = buffers [0].len;

        if (g_debugSend)
                debugWrite ("WSASend", buf, len);

        buf = filterHttpUrl (s, buf, len);

        if (len == 0) {
                len = buffers [0].len;

                OutputDebugStringA ("Substituting HTTP request\r\n");
                if (overlapped != 0) {
                        overlapped->Internal = ERROR_SUCCESS;
                        overlapped->InternalHigh = len;

                        HANDLE          event = overlapped->hEvent;
                        event = (HANDLE) ((unsigned long) event & ~ 1);
                        if (event != 0)
                                SetEvent (event);
                }

                if (sent != 0)
                        * sent = len;
                return 0;
        }

        if (buf == 0) {
                SetLastError (WSAECONNRESET);
                return SOCKET_ERROR;
        }

        /*
         * Pass-through is the simple case.
         */

        if (buf == buffers [0].buf) {
                return (* g_wsaSendHook) (s, buffers, count, sent, flags,
                                          overlapped, handler);
        }

        /*
         * If the URL was rewritten, things are complex if we want to mimic the
         * action of the underlying API faithfully to the caller - especially
         * if the caller is using overlapped I/O and even more if completion
         * ports are involved (they are excellent, just not for what we're in
         * the process of doing here).
         *
         * If I decide to go for some form of persistent context tracking for
         * sockets that makes all that easier, especially since we can just do
         * a synchronous success to the caller and manage the rest in the
         * background.
         *
         * However, for now since the Steam client does synchronous sends with
         * just one buffer structure, that's all I'll support.
         */

        if (overlapped != 0 || handler != 0 || count > 1) {
                HeapFree (GetProcessHeap (), 0, (LPVOID) buf);
                SetLastError (WSAEINVAL);
                return SOCKET_ERROR;
        }

        WSABUF          temp [1] = { len, (char *) buf };
        int             result;
        unsigned long   actual;
        result = (* g_wsaSendHook) (s, temp, 1, & actual, flags, 0, 0);

        HeapFree (GetProcessHeap (), 0, (LPVOID) buf);
        if (result != 0)
                return result;

        * sent = buffers [0].len;
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
 * @{
 */

#define PUSH_IMM8       0x6A
#define JMP_LONG        0xE9
#define JMP_SHORT       0xEB

#define MOV_EDI_EDI     0xFF8B
#define JMP_SHORT_MINUS5 (0xF900 + JMP_SHORT)

#define JMP_INDIRECT    0x25FF

/**@}*/

/**
 * Record our HMODULE value for unloading.
 */

HMODULE         g_instance;

/**
 * Set up the address to direct the content server connections to.
 */

int setFilter (wchar_t * address) {
        bool            result = g_rules.install (address);
        if (result) {
                /*
                 * For now, always append these black-hole rules to the main
                 * rule set, along with a couple of rules to try and manage the
                 * "CS" type servers out of use and as well eliminate any
                 * server listed in a special server list that hasn't already
                 * been whitelisted.
                 *
                 * Since rules are processed in order, this still allows custom
                 * rules to redirect these DNS lookups to take place, as those
                 * will take precedence to this catch-all; in a rule list, the
                 * first rule that matches stops further search.
                 */

                g_rules.append (L"//*.steampowered.com=*;"
                                L"//*/depot/*=;"
                                L"/initsession/=");
        }

        return result ? 1 : 0;
}

/**
 * Copy some code from a patch target into a thunk.
 *
 * This is used if the target doesn't contain the patch NOP; we copy the code
 * to a temporary area so that when we resume the original function, we call
 * the relocated code which then branches back to the original flow.
 *
 * This relies on being able to measure instruction lengths to a degree to know
 * how much to relocate; doing this in general for x86 isn't too bad, but since
 * function entry points are highly idiomatic we probably won't need to solve
 * the general problem (I've written a full general patcher that does this in
 * the past, but don't have access to that code anymore).
 */

FARPROC ApiHook :: makeThunk (unsigned char * data, size_t bytes) {
        memcpy (m_thunk, data, bytes);

        m_thunk [bytes] = JMP_LONG;

        /*
         * The offset for the branch is relative to the end of the decoded
         * branch instruction; it has to go to (data + bytes) to skip over
         * the things that we've copied to the thunk.
         */

        unsigned char * addr = m_thunk + bytes;
        writeOffset (addr + 1, (data + bytes) - (addr + 5));

        unsigned long   protect = 0;
        if (! VirtualProtect (m_thunk, sizeof (m_thunk),
                              PAGE_EXECUTE_READWRITE, & protect))
                return 0;

        return (FARPROC) & m_thunk;
}

/**
 * Use the built-in Windows run-time patching system for core DLLs.
 *
 * In almost all cases this works fine; there are a few stray APIs in the Win32
 * ecosystem where the initial two-byte NOP isn't present (e.g. inet_addr) and
 * so some form of thunk-based redirection can be needed.
 *
 * For the inet_addr case the 5 bytes of regular NOP space is still there to
 * hold a JMP thunk, just the initial MOV EDI, EDI NOP isn't there so when I
 * write in the short-form JMP -5 I have to copy the initial instruction to
 * an indirect thunk.
 *
 * Worse, there is a lame piece of firewall code that already squats on the
 * basic Windows hook system in at least one user's system. That needs more
 * than the first two instructions in the source to be preserved.
 */

bool ApiHook :: attach (void * address, FARPROC hook) {
        if (address == 0)
                return false;

        m_hook = hook;
        m_original = (FARPROC) address;

        /*
         * Check for the initial MOV EDI, EDI two-byte NOP in the target
         * function, to signify the presence of a free patch area.
         *
         * We'll rely on the x86's support for unaligned access here, as we're
         * always going to be doing this on an x86 or something with equivalent
         * support for unaligned access.
         */

        unsigned char * data = (unsigned char *) address;
        memcpy (m_save, data - 5, 8);
        m_resume = 0;

        unsigned short  word = * (unsigned short *) data;
        if (word == MOV_EDI_EDI) {
                /*
                 * No need for a thunk, the resume point can be where we want.
                 */

                m_resume = (FARPROC) (data + 2);
        } else if (* data == PUSH_IMM8) {
                /*
                 * For inet_addr where the initial hook MOV, EDI, EDI is absent
                 * but the hook region is still there.
                 */

                m_resume = makeThunk (data, 2);
        } else if (word == JMP_INDIRECT) {
                /*
                 * This is used by some rather lame API rewriting done by very
                 * obscure code called Emsisoft Anti-Malware. It's really easy
                 * to defeat if you know it's there, but it's probably relying
                 * on being so obscure and used by so few people that there's
                 * not really any point for actual malware to disable it.
                 */

                m_resume = makeThunk (data, 6);
        } else
                return false;

        /*
         * Write a branch to the hook stub over the initial NOP (or other code,
         * if the function is already detoured somehow) of the target.
         */

        unsigned long   protect = 0;
        if (! VirtualProtect (data - 5, 7, PAGE_EXECUTE_READWRITE, & protect))
                return false;

        /*
         * Put the long jump to the detour first (in space which is reserved
         * for just this purpose in code compiled for patching), then put the
         * short branch to the long jump in the two-byte slot at the regular
         * function entry point.
         *
         * If there's a previous detour which used the hook slot, then above it
         * would have been saved and we'll use that as the continue point. If
         * something else entirely is going on we'll have built a special thunk
         * in m_thunk for the continue point.
         */

        data [- 5] = JMP_LONG;
        writeOffset (data - 4, (unsigned char *) hook - data);
        * (unsigned short *) data = JMP_SHORT_MINUS5;

        return true;
}

/**
 * Add some idiomatic wrapping for using GetProcAddress.
 */

bool ApiHook :: attach (void * hook, HMODULE lib, const char * name) {
        FARPROC         func = GetProcAddress (lib, name);
        if (! func) {
                OutputDebugStringA ("No function: ");
                OutputDebugStringA (name);
                OutputDebugStringA ("\r\n");

                m_resume = 0;
                return false;
        }

        if (! attach (func, (FARPROC) hook)) {
                OutputDebugStringA ("Can't hook: ");
                OutputDebugStringA (name);
                OutputDebugStringA ("\r\n");

                m_resume = 0;
                return false;
        }

        return true;
}

/**
 * Remove an attached hook.
 *
 * In case the target DLL is actually unloaded, the write to the detour point
 * is guarded with a Win32 SEH block to avoid problems with the writes.
 */

void ApiHook :: unhook (void) {
        if (m_resume == 0)
                return;

        __try {
                memcpy ((unsigned char *) m_original - 5, m_save, 7);
        } __finally {
                m_original = m_resume = 0;
        }
}

/**
 * Global counter manipulated by InHook as hook functions are entered to make
 * loading and unloading safer.
 */

/* static */
LONG            InHook :: g_hookCount;

/**
 * Unhook all the hooked functions.
 */

void unhookAll (void) {
        g_connectHook.unhook ();
        g_gethostHook.unhook ();
        g_recvHook.unhook ();
        g_recvfromHook.unhook ();
        g_wsaRecvHook.unhook ();
        g_select_Hook.unhook ();
        g_sendHook.unhook ();
        g_closesocket_Hook.unhook ();
        g_wsaEventSelectHook.unhook ();
        g_wsaGetOverlappedHook.unhook ();
        g_wsaEnumNetworkEventsHook.unhook ();
        g_wsaSendHook.unhook ();

        /*
         * Wait until none of the main application threads are inside any of
         * the hook routines.
         */

        while (InHook :: g_hookCount > 0)
                Sleep (1);
}

/**
 * Simple default constructor.
 */

ApiHook :: ApiHook () : m_original (0), m_resume (0), m_hook (0) {
}

/**
 * Simple default destructor.
 */

ApiHook :: ~ ApiHook () {
        unhook ();
}

/**
 * Simple attach.
 *
 * In principle in future this should lead to the hook being attached to a list
 * of all currently hooked functions.
 */

template <class F>
bool Hook<F> :: attach (F hook, HMODULE lib, const char * name) {
        return ApiHook :: attach (hook, lib, name);
}

/**
 * Establish the hook filter we want on the connect function in WS2_32.DLL
 */

STEAMDLL (int) SteamFilter (wchar_t * address, wchar_t * result,
                            size_t * resultSize, HKEY rootKey,
                            const wchar_t * rootReg, const wchar_t * rootDir) {
        /*
         * If we've already been called, this is a call to re-bind the address
         * being monitored.
         */

        if (g_connectHook != 0)
                return setFilter (address);

        /*
         * Wait for the target module to be present, so as not to interfere
         * with any loading or initialization process in the host process.
         */

        HMODULE         ws2;
        for (;;) {
                ws2 = GetModuleHandleW (L"WS2_32.DLL");
                if (ws2 != 0)
                        break;

                Sleep (1000);
        }

        g_initReplacement (rootKey, rootReg);

        setFilter (address);

        bool            success;
        success = g_connectHook.attach (connectHook, ws2, "connect") &&
                  g_gethostHook.attach (gethostHook, ws2, "gethostbyname") &&
                  g_recvHook.attach (recvHook, ws2, "recv") &&
                  g_recvfromHook.attach (recvfromHook, ws2, "recvfrom") &&
                  g_wsaRecvHook.attach (wsaRecvHook, ws2, "WSARecv") &&
                  g_select_Hook.attach (select_Hook, ws2, "select") &&
                  g_sendHook.attach (sendHook, ws2, "send") &&
                  g_closesocket_Hook.attach (closesocket_Hook, ws2,
                                             "closesocket") && 
                  g_wsaEventSelectHook.attach (wsaEventSelectHook, ws2,
                                               "WSAEventSelect") &&
                  g_wsaGetOverlappedHook.attach (wsaGetOverlappedHook,
                                                 ws2, "WSAGetOverlappedResult") &&
                  g_wsaEnumNetworkEventsHook.attach (wsaEnumNetworkEventsHook,
                                                     ws2, "WSAEnumNetworkEvents") &&
                  g_wsaSendHook.attach (wsaSendHook, ws2, "WSASend");

        g_getpeername = (getpeernameFunc) GetProcAddress (ws2, "getpeername");

        if (! success) {
                unhookAll ();
                return ~ 0UL;
        }

        OutputDebugStringA ("SteamFilter " VER_PRODUCTVERSION_STR " attached\n");

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
        if (g_connectHook == 0)
                return;

        unhookAll ();
        g_unloadReplacement ();
        OutputDebugStringA ("SteamFilter " VER_PRODUCTVERSION_STR " unhooked\n");
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
