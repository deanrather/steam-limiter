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
 * The prototype of the legacy sockets inet_addr () function.
 */

typedef unsigned long (WSAAPI * inet_addr_func) (const char * addr);

/**
 * The prototype of the legacy sockets recv () function.
 */

typedef int   (WSAAPI * RecvFunc) (SOCKET s, char * buf, int len, int flags);

/**
 * The prototype of the legacy sockets recvfrom () function.
 */

typedef int   (WSAAPI * RecvFromFunc) (SOCKET s, char * buf, int len, int flags,
                                       sockaddr * from, int * fromLen);

/**
 * The prototype of the modern asynchronous WSARecv () function.
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
 * The prototype of the modern asynchronous WSASend () function. 
 */

typedef int   (WSAAPI * WSASendFunc) (SOCKET s, LPWSABUF buffers,
                                      unsigned long count,
                                      unsigned long * sent, unsigned long flags,
                                      OVERLAPPED * overlapped,
                                      LPWSAOVERLAPPED_COMPLETION_ROUTINE handler);

/**
 * The prototype of WSAGetOverlappedResult (), used with WSASend () and
 * WSARecv ().
 */

typedef BOOL  (WSAAPI * WSAGetOverlappedFunc) (SOCKET s, OVERLAPPED * overlapped,
                                               unsigned long * length, BOOL wait,
                                               unsigned long * flags);

/**
 * Prototype for getpeername (), which we don't hook but do want to call.
 */

typedef int   (WSAAPI * getpeernameFunc) (SOCKET s, sockaddr * addr, int * length);

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
Hook<inet_addr_func>    g_inet_addr_Hook;
Hook<RecvFunc>          g_recvHook;
Hook<RecvFromFunc>      g_recvfromHook;
Hook<WSARecvFunc>       g_wsaRecvHook;
Hook<WSAGetOverlappedFunc> g_wsaGetOverlappedHook;
Hook<SendFunc>          g_sendHook;
Hook<WSASendFunc>       g_wsaSendHook;

getpeernameFunc         g_getpeername;

/**@}*/

/**
 * This holds all the rules we apply.
 */

FilterRules     g_rules (27030);

/**
 * Hook for the connect () function; check if we want to rework it, or just
 * continue on to the original.
 *
 * Mainly our aim was to block port 27030 which is the good Steam 'classic' CDN
 * but Valve have *two* amazingly half-baked and badly designed HTTP download
 * systems as well. One - "CDN" - uses DNS tricks, while the other one - "CS" -
 * is rather more nasty, and almost impossible to filter cleanly out from the
 * legitimate use of HTTP inside Steam - for CS servers only numeric IPs are
 * passed over HTTP and they aren't parsed by API functions we can hook like
 * RtlIpv4AddressToStringEx (which exists in Windows XPSP3 even though it's
 * not documented).
 */

int WSAAPI connectHook (SOCKET s, const sockaddr * name, int namelen) {
        /*
         * Capture the caller's return address so we can map it to a module and
         * thus a module name, to potentially include in the filter string.
         *
         * An alternative to this is to use RtlCaptureStackBacktrace to get the
         * caller (and potentially more of the stack), but sinec we're x86 only
         * for now this should do fine.
         */

        HMODULE         module = 0;

#if     0
        unsigned long   addr = ((unsigned long *) & s) [- 1];
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR) addr, & module);
#endif

        sockaddr_in   * replace = 0;
        if (name->sa_family != AF_INET ||
            ! g_rules.match ((const sockaddr_in *) name, module, & replace)) {
                /*
                 * Just forward on to the original.
                 */

                return (* g_connectHook) (s, name, namelen);
        }

        /*
         * If no replacement is specified, deny the connection. This isn't used
         * for Steam blocking in most cases because it responds to this by just
         * trying another server.
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
        wsprintfA (show, "Connect redirected to %d.%d.%d.%d:%d\r\n",
                   bytes [0], bytes [1], bytes [2], bytes [3],
                   ntohs (temp.sin_port));

        OutputDebugStringA (show);
                
        return (* g_connectHook) (s, (sockaddr *) & temp, sizeof (temp));
}

/**
 * Hook for the gethostbyname () Sockets address-resolution function.
 */

struct hostent * WSAAPI gethostHook (const char * name) {
        sockaddr_in   * replace = 0;
        if (! g_rules.match (name, & replace) ||
            (replace != 0 && replace->sin_addr.S_un.S_addr == INADDR_ANY)) {
                /*
                 * If there's no matching rule, or the matching rule is a
                 * passthrough, then let things slide.
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

                OutputDebugStringA ("gethostbyname refused\r\n");
                SetLastError (WSAHOST_NOT_FOUND);
                return 0;
        }

#if     0
        /*
         * This gets called a *lot* during big downloads, so it's less useful
         * having it here now, especially since the port 27030 CDN is now very
         * much deprecated.
         */

        OutputDebugStringA ("gethostbyname redirected\r\n");
#endif

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

static  hostent         result;
static  unsigned long   addr;
static  unsigned long * addrList [2] = { & addr };

        addr = replace->sin_addr.S_un.S_addr;
        result.h_addrtype = AF_INET;
        result.h_addr_list = (char **) addrList;
        result.h_aliases = 0;
        result.h_length = sizeof (addr);
        result.h_name = "remapped.local";

        return & result;
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
        int             result;
        result = (* g_recvHook) (s, buf, len, flags);
        g_meter += result;
        return result;
}

int WSAAPI recvfromHook (SOCKET s, char * buf, int len, int flags,
                         sockaddr * from, int * fromLen) {
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

int WSAAPI wsaRecvHook (SOCKET s, LPWSABUF buffers, unsigned long count,
                        unsigned long * received, unsigned long * flags,
                        OVERLAPPED * overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE handler) {
        if (overlapped != 0 || handler != 0) {
                int             result;
                result = (* g_wsaRecvHook) (s, buffers, count, received, flags,
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
        result = (* g_wsaRecvHook) (s, buffers, count, received, flags,
                                      overlapped, handler);
        if (result != SOCKET_ERROR && ! ignore)
                g_meter += * received;
        return result;
}

BOOL WSAAPI wsaGetOverlappedHook (SOCKET s, OVERLAPPED * overlapped,
                                  unsigned long * length, BOOL wait,
                                  unsigned long * flags) {
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
 * Apply URL filters.
 */

const char * filterHttpUrl (SOCKET s, const char * buf, size_t & length) {
        if (length < 10)
                return buf;

        /*
         * The Steam client uses upper-case verb text, so we keep things simple.
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

        if (hostLength > 0) {
                * dest = '[';
                memcpy (dest + 1, host, hostLength - 2);
                dest [hostLength - 1] = ']';
                dest += hostLength;
        }

        const char    * urlPart = dest;
        memcpy (dest, buf + verb, tempLen - verb);
        dest += tempLen - verb;

        strcpy (dest, "\r\n");

        OutputDebugStringA (temp);
        * dest = 0;

        /*
         * Now run the match against the URL.
         */

        const char    * replace = 0;
        if (! g_rules.match (urlPart, & replace)) {
                /*
                 * If I wanted, instead of passing through now that I'm getting
                 * the host data out, I can try a different match against just
                 * the host part of the string - I'm curious if I might want to
                 * do this to more firmly nail down the "CS" content server
                 * type.
                 */

                return buf;
        }

        /*
         * An empty pattern means block.
         */

        if (replace == 0 || * replace == 0)
                return 0;

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


        size_t          subst = strlen (replace);
        size_t          rest = (buf + length - end);
        size_t          total = verb + subst + rest;
        char          * copy = (char *) HeapAlloc (GetProcessHeap (), 0, total + 1);

        memcpy (copy, buf, verb);
        memcpy (copy + verb, replace, subst);
        memcpy (copy + verb + subst, end, rest);
        copy [total] = 0;

        OutputDebugStringA (copy);
        return copy;
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
        const char    * buf = buffers [0].buf;
        size_t          len = buffers [0].len;
        buf = filterHttpUrl (s, buf, len);

        if (buf == 0) {
                OutputDebugStringA ("Blocking HTTP request\r\n");
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
                SetLastError (WSAEINVAL);
                return SOCKET_ERROR;
        }

        WSABUF          temp [1] = { len, (char *) buf };
        int             result;
        unsigned long   actual;
        result = (* g_wsaSendHook) (s, temp, 1, & actual, flags, 0, 0);
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
                 * For now, always append this black-hole DNS rule to the main
                 * rule set. Later on this might change but this will do until
                 * the full situation with the HTTP CDN is revealed.
                 *
                 * Since rules are processed in order, this still allows custom
                 * rules to redirect these DNS lookups to take place, as those
                 * will take precedence to this catch-all.
                 */

                g_rules.append (L"content?.steampowered.com=;/initsession/=");
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
        writeOffset (m_thunk + bytes + 1, m_thunk - data);

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
 * Unhook all the hooked functions.
 */

void unhookAll (void) {
        g_connectHook.unhook ();
        g_gethostHook.unhook ();
        g_inet_addr_Hook.unhook ();
        g_recvHook.unhook ();
        g_recvfromHook.unhook ();
        g_wsaRecvHook.unhook ();
        g_wsaGetOverlappedHook.unhook ();
        g_sendHook.unhook ();
        g_wsaSendHook.unhook ();
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
                            size_t * resultSize) {
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

        setFilter (address);

        bool            success;
        success = g_connectHook.attach (connectHook, ws2, "connect") &&
                  g_gethostHook.attach (gethostHook, ws2, "gethostbyname") &&
                  g_recvHook.attach (recvHook, ws2, "recv") &&
                  g_recvfromHook.attach (recvfromHook, ws2, "recvfrom") &&
                  g_wsaRecvHook.attach (wsaRecvHook, ws2, "WSARecv") &&
                  g_wsaGetOverlappedHook.attach (wsaGetOverlappedHook,
                                                   ws2, "WSAGetOverlappedResult") &&
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
