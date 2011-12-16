/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * This defines a data structure and parser for rules to use in connection
 * and DNS filtering.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
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

#include <winsock2.h>
#include <ws2tcpip.h>

#include "filterrule.h"
#include "glob.h"

/**
 * Cliche for measuring array lengths, to avoid mistakes with sizeof ().
 */

#define ARRAY_LENGTH(x) (sizeof (x) / sizeof (* (x)))

/**
 * Avoid the standard VC++ new and delete since they are throwing, so using
 * them would be insane.
 *
 * Instead just rely on the Win32 heap routines, since they're pretty much all
 * that the VC++ malloc ()/free () use these days anyway.
 */

/* static */
void * operator new (size_t size) throw () {
        return HeapAlloc (GetProcessHeap (), 0, size);
}

/* static */
void operator delete (void * mem) {
        HeapFree (GetProcessHeap (), 0, mem);
}

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
 * Simple default constructor.
 */

FilterRule :: FilterRule () : m_pattern (0), m_hasPort (false), m_port (0),
                m_replace (0), m_nextReplace (0), m_next (0) {
}

/**
 * Clean up memory allocated to the pattern string and addresses.
 */

FilterRule :: ~ FilterRule () {
        freeInfo (m_replace);

        if (m_pattern != 0)
                free (m_pattern);
}

/**
 * Clean up memory allocated to the address set.
 *
 * This is simplified by virtue of parseReplace () allocating both the info and
 * address structures in the one allocation call.
 */

void FilterRule :: freeInfo (addrinfo * info) {
        addrinfo      * temp;
        while ((temp = info) != 0) {
                info = temp->ai_next;
                free (temp);
        }
}

/**
 * Basically the same as wcschr (), but aware of glob escapes.
 */

/* static */
const wchar_t * FilterRule :: lookahead (const wchar_t * from, const wchar_t * to,
                                         wchar_t ch) {
        for (; from != to ;) {
                wchar_t         temp = * from;
                if (temp == ch)
                        return from;

                if (temp == '\\') {
                        ++ from;
                        if (from == to)
                                return 0;

                        temp = * ++ from;
                }

                if (temp == 0)
                        return 0;

                ++ from;
        }

        return 0;
}

/**
 * Companion to lookahead, this extracts a potentially escaped sequence of data
 * into a buffer.
 *
 * Because we're intermingling things that are glob-type patterns with higher-
 * level syntax and things that are meant to be literals, we're using the same
 * C-style escaping convention for all the elements. This means that to work
 * with any literal text we have to unescape it.
 */

/* static */
wchar_t * FilterRule :: unescape (wchar_t * dest, size_t length,
                                  const wchar_t * from, const wchar_t * to) {
        wchar_t       * base = dest;
        if (length == 0)
                return 0;

        /*
         * Pre-reserve space for a null terminator at the end of the output
         * buffer.
         */

        -- length;

        for (; from != to ;) {
                wchar_t         temp = * from;
                if (temp == '\\') {
                        ++ from;
                        if (from == to)
                                break;

                        temp = * from;
                }

                if (temp == 0)
                        break;

                ++ from;

                if (length == 0)
                        return 0;

                -- length;

                * dest = temp;
                ++ dest;
        }

        * dest = 0;
        return base;
}

/**
 * Similar to wcsdup () but take the input as a boundary pair.
 */

wchar_t * FilterRule :: wcsdup (const wchar_t * from, const wchar_t * to) {
        if (to == 0)
                return :: wcsdup (from);

        size_t          size = (to - from + 1) * sizeof (wchar_t);
        wchar_t       * temp = (wchar_t *) malloc (size);
        memcpy (temp, from, size);

        /*
         * Ensure the new string is always null-terminated.
         */

        temp [to - from] = 0;
        return temp;
}

/**
 * Look for a port specification in a pattern or replacement specification and
 * extract it.
 */

const wchar_t * FilterRule :: hasPort (const wchar_t * from, const wchar_t * to,
                                       unsigned short & port) {
        if (from == 0)
                return 0;

        const wchar_t * portSpec = lookahead (from, to, ':');
        if (portSpec == 0)
                return to;

        /*
         * Effectively remove the trailing portspec from the incoming rule.
         */

        to = portSpec;
        port = 0;

        /*
         * Here we could look for special cases in future: for now we only use
         * an empty portspec and a 0 as the same thing, a wildcard, but perhaps
         * we'll do more in this respect one day.
         */

        int             radix = 10;
        wchar_t         ch;
        while (portSpec != to && (ch = * ++ portSpec) != 0) {
                /*
                 * What to do about invalid input? For now, since there's no
                 * effective way to report errors, just ignore it.
                 */

                ch -= '0';
                if (ch > 9)
                        break;

                port = port * radix + ch;
        }

        return to;
}

/**
 * Parse one of the a replacement items for a rule.
 *
 * Both of the things which actually perform replacement deal in IP addresses
 * so although we can permit hostnames here, we have to resolve them. The
 * GetAddrInfoW () function can handle either numeric or DNS names, so just use
 * that to deal with either.
 *
 * Ideally we want to use a global function pointer to indirect through, just
 * in case we're in a context where we might want to be using filters to remap
 * GetAddrInfoW () results, or where we might be loaded into a process before
 * Winsock is initialized.
 *
 * For now we're only going to be remapping IPv4 addresses since the only
 * applications we're going to be used for are IPv4-only.
 */

bool FilterRule :: parseReplace (const wchar_t * from, const wchar_t * to,
                                 addrinfo * & link) {
        size_t          size = sizeof (addrinfo) + sizeof (sockaddr_in);
        void          * mem = malloc (size);
        memset (mem, 0, size);

        /*
         * Allow replacement rules to have comment fields (or indeed, allow
         * them to be completely commented out).
         */

        const wchar_t * comment = lookahead (from, to, '#');
        if (comment != 0)
                to = comment;

        addrinfo      * temp = (addrinfo *) mem;
        sockaddr_in   * addr = (sockaddr_in *) (temp + 1);

        temp->ai_addr = (sockaddr *) addr;
        temp->ai_addrlen = sizeof (* addr);
        temp->ai_family = AF_INET;
        temp->ai_flags = 0;
        temp->ai_next = link;

        const wchar_t * port = hasPort (from, to, addr->sin_port);
        if (port == to) {
                addr->sin_port = 0;
        } else
                to = port;

        wchar_t         text [40];
        from = unescape (text, ARRAY_LENGTH (text), from, to);
        if (from == 0) {
                free (mem);
                return false;
        }

        for (;;) {
                /*
                 * Filter whitespace, but don't use the regular C library
                 * character classification, as the multibyte support in the
                 * VC++ RTL is a bloated monstrosity and we don't want it.
                 */

                wchar_t         ch = * from;
                switch (ch) {
                case 0:
                        /*
                         * An empty element in a replacement list means block.
                         */

                        addr->sin_addr.S_un.S_addr = INADDR_NONE;
                        link = temp;
                        return true;

                case '*':
                        /*
                         * A * in a replacement list means pass through unchanged.
                         */

                        addr->sin_addr.S_un.S_addr = INADDR_ANY;
                        link = temp;
                        return true;

                case '\n':
                case '\r':
                case '\t':
                case ' ':
                        ++ from;
                        continue;

                default:
                        break;
                }

                break;
        }

        ADDRINFOW     * wide = 0;
        unsigned long   result = (* g_addrFunc) (from, 0, 0, & wide);
        if (result != 0) {
                /*
                 * For now, rather than failing things we make failed address
                 * resolution result in no replacement.
                 */

                free (mem);
                return true;
        }

        /*
         * Pick the first available IPv4 address from the returned list, as
         * while we expect only one it's conceivable an IPv6 could result when
         * this is being used in very general cases.
         */

        ADDRINFOW     * choices = wide;
        while (choices->ai_addr->sa_family != AF_INET) {
                /*
                 * If we run out of options, map to nowhere.
                 */

                if ((choices = choices->ai_next) == 0) {
                        free (mem);
                        return true;
                }
        }

        sockaddr_in   * chosen = (sockaddr_in *) choices->ai_addr;
        addr->sin_addr = chosen->sin_addr;

#if     1
        char            example [80];
        wsprintfA (example, "%ls=%d.%d.%d.%d\r\n", from,
                   chosen->sin_addr.S_un.S_un_b.s_b1,
                   chosen->sin_addr.S_un.S_un_b.s_b2,
                   chosen->sin_addr.S_un.S_un_b.s_b3,
                   chosen->sin_addr.S_un.S_un_b.s_b4);

        OutputDebugStringA (example);
#endif

        if (wide != 0)
                (* g_freeFunc) (wide);

        link = temp;
        return true;
}

/**
 * Parse the specification for an individual rule.
 *
 * The grammar for a rule looks roughly like this:
 *      rule    ::== <replace> (',' <replace>)*
 *      rule    ::== <pattern> '=' [<replace> (',' <replace>)*]
 *      replace ::== <host> [':' <port>]
 *      pattern ::== <glob> [':' <port>]
 */

bool FilterRule :: parseRule (const wchar_t * from, const wchar_t * to) {
        /*
         * Split out the pattern from the rest of the rule now, to help make it
         * easier to extract the port from the pattern.
         *
         * But what does it mean if there's no '='? Either we have a pattern
         * with no replacement (i.e., the replacement is empty) or an empty
         * pattern (i.e., a wildcard) with just a replacement.
         *
         * Which interpretation makes more sense? Here we're guided by the fact
         * that the simplest thing to do is to have a default port implied for
         * a rule, and for the specification to be the target to rewrite to.
         *
         * This is supported by the fact rewriting things is more interesting
         * than blocking them, which is indeed how this all got started.
         */

        const wchar_t * replace = lookahead (from, to, '=');
        const wchar_t * replaceTo = 0;

        if (replace == 0) {
                replace = from;
                replaceTo = to;
                to = from = 0;
        } else {
                replaceTo = to;
                to = replace;
                ++ replace;
        }

        /*
         * See whether the pattern has a port specification; if so, we'll make
         * note of that since it can be handy to distinguish connect rules and
         * DNS rules, since DNS rules don't use ports.
         */

        m_hasPort = hasPort (from, to, m_port) != to;

        /*
         * Duplicate the rest of the pattern, if there is one.
         */

        m_pattern = from != 0 && * from != 0 ? wcsdup (from, to) : 0;

        /*
         * Now, turn the replacement specs into a sequence of addrinfo data.
         *
         * Note that since we're using glob patterns, something we don't have
         * in the replacements is an equivalent to backreferences. Those are
         * actually something that is possible - our glob matcher could make
         * wildcards capture their text - but in the absence of an immediate
         * obvious reason for them, it seems better to leave that.
         */

        addrinfo      * tail = 0;
        for (; replace != replaceTo ;) {
                const wchar_t * next = lookahead (replace, replaceTo, ',');

                addrinfo     ** dest = tail == 0 ? & m_replace :
                                       & tail->ai_next;

                parseReplace (replace, next != 0 ? next : replaceTo, * dest);
                tail = * dest;

                if (next == 0)
                        break;

                replace = next + 1;
        }

        return true;
}

/**
 * Match a filter rule based on the text string.
 */

bool FilterRule :: match (const char * example, addrinfo ** replace) {
        if (m_pattern != 0 && ! globMatch (example, m_pattern))
                return false;

        /*
         * OK, a match. If there is no replacement, say so, otherwise return
         * a suitable replacement and adjust the replacement chain so that the
         * replacements are rotated through.
         */

        addrinfo      * next = m_nextReplace;
        if (next == 0)
                next = m_replace;

        if ((* replace = next) != 0 && (next = next->ai_next) == 0)
                next = m_replace;

        m_nextReplace = next;
        return true;
}


/**
 * Lock for controlling access to the list of rules within a rule set, since
 * the list is accessed from multiple threads.
 */

CRITICAL_SECTION        l_filterLock [1];

/**
 * Initialise function pointers to the Winsock address-info functions.
 */

static bool l_initFuncs (void) {
        if (g_addrFunc != 0 && g_freeFunc != 0)
                return true;

        InitializeCriticalSection (l_filterLock);

        HMODULE         ws2 = GetModuleHandle (L"WS2_32.DLL");
        if (ws2 == 0)
                return false;

        g_addrFunc = (GetAddrInfoWFunc) GetProcAddress (ws2, "GetAddrInfoW");
        g_freeFunc = (FreeAddrInfoWFunc) GetProcAddress (ws2, "FreeAddrInfoW");

        return g_addrFunc != 0 && g_freeFunc != 0;
}

/**
 * Simple constructor for the rule list.
 */

FilterRules :: FilterRules (unsigned short defaultPort) :
                m_head (0), m_tail (0), m_defaultPort (defaultPort) {
}

/**
 * Simple destructor for the rule list.
 */

FilterRules :: ~ FilterRules () {
        freeRules (m_head);
}

/**
 * Parse a new filter specification.
 *
 * The filter specification is roughly like this:
 *      rules   ::== <rule> (';' <rule>)*
 */

bool FilterRules :: parse (const wchar_t * from, const wchar_t * to,
                           FilterRule * & head, FilterRule * & tail) {
        if (to == 0 && from != 0)
                to = from + wcslen (from);

        while (from != to) {
                /*
                 * Filter whitespace, but don't use the regular C library
                 * character classification, as the multibyte support in the
                 * VC++ RTL is a bloated monstrosity and we don't want it.
                 */

                wchar_t         ch = * from;
                switch (ch) {
                case 0:
                        return true;

                case '\n':
                case '\r':
                case '\t':
                case ' ':
                        ++ from;
                        continue;

                default:
                        break;
                }

                /*
                 * Peek ahead to see where the end of the top-level term is,
                 * if there is one, and split the input there to do a simple
                 * recursion (as usual here, I avoid fancy techniques to keep
                 * the code size down).
                 */

                const wchar_t * next = FilterRule :: lookahead (from, to, ';');
                const wchar_t * nextTo;
                if (next != 0) {
                        nextTo = to;
                        to = next;
                        ++ next;
                } else {
                        nextTo = next = 0;
                }

                FilterRule    * temp = new FilterRule;
                if (tail != 0) {
                        tail->m_next = temp;
                } else
                        head = temp;
                tail = temp;

                if (! temp->parseRule (from, to)) {
                        /*
                         * Hard to know whether to abort totally on a failed
                         * rule or return a partial set. Failing totally is
                         * probably easier for now.
                         */

                        freeRules (head);
                        return false;
                }

                /*
                 * If no port was specified and there's no pattern (which is in
                 * essence a wildcard), use the default port.
                 *
                 * This helps grandfather in a simple IP-only specification for
                 * what to redirect to in cases where the port is implied.
                 */

                if (! temp->m_hasPort && temp->m_pattern == 0) {
                        temp->m_hasPort = true;
                        temp->m_port = m_defaultPort;
                }

                from = next;
                to = nextTo;
        }

        return true;
}

/**
 * Free a list of filter rules.
 */

/* static */
void FilterRules :: freeRules (FilterRule * head) {
        while (head != 0) {
                FilterRule    * next = head->m_next;
                delete head;
                head = next;
        }
}

/**
 * Create a fresh set of filter rules from a spec string.
 */

/* static */
bool FilterRules :: install (const wchar_t * specs) {
        if (! l_initFuncs ())
                return false;

        FilterRule    * head = 0;
        FilterRule    * tail = 0;

        if (! parse (specs, 0, head, tail))
                return false;

        EnterCriticalSection (l_filterLock);

        FilterRule    * temp = m_head;
        m_head = head;
        m_tail = tail;

        LeaveCriticalSection (l_filterLock);

        /*
         * Strictly speaking, this is bad since the a piece of matched rule
         * data can have escaped from a match () function in a thread. Delaying
         * the actual free for one round would help with that.
         */

        freeRules (temp);
        return true;
}

/**
 * Add additional rules to an existing set.
 */

/* static */
bool FilterRules :: append (const wchar_t * specs) {
        if (! l_initFuncs ())
                return false;


        EnterCriticalSection (l_filterLock);

        bool            result;
        result = parse (specs, 0, m_head, m_tail);

        LeaveCriticalSection (l_filterLock);
        return result;
}

/**
 * Simple equivalent to ntohs.
 *
 * I don't want a static DLL dependency against ntohs () and this is easier
 * than making the dependency fully dynamic.
 */

#define ntohs(x)        ((unsigned char) ((x) >> 8) + \
                         ((unsigned char) (x) << 8))

/**
 * Match the filter rules against an address.
 *
 * Since the main spec strings used to match rules are glob patterns, the IPv4
 * address is quickly rendered as text for matching.
 */

bool FilterRules :: match (const sockaddr_in * name, sockaddr_in ** replace) {
        unsigned short  port = ntohs (name->sin_port);
        char            example [24];

        wsprintfA (example, "%d.%d.%d.%d:%d",
                        name->sin_addr.S_un.S_un_b.s_b1,
                        name->sin_addr.S_un.S_un_b.s_b2,
                        name->sin_addr.S_un.S_un_b.s_b3,
                        name->sin_addr.S_un.S_un_b.s_b4,
                        port);

        EnterCriticalSection (l_filterLock);

        FilterRule    * test = m_head;
        addrinfo      * out = 0;
        for (; test != 0 ; test = test->m_next) {
                if (! test->m_hasPort)
                        continue;

                if (test->m_port != 0 && test->m_port != port)
                        continue;

                if (test->match (example, & out))
                        break;
        }

        LeaveCriticalSection (l_filterLock);

        if (out != 0) {
                * replace = (sockaddr_in *) out->ai_addr;
        } else
                * replace = 0;

        return test != 0;
}

/**
 * Match the filter rules against a DNS name.
 */

bool FilterRules :: match (const char * name, sockaddr_in ** replace) {
        EnterCriticalSection (l_filterLock);

        FilterRule    * test = m_head;
        addrinfo      * out = 0;
        for (; test != 0 ; test = test->m_next) {
                if (test->m_hasPort)
                        continue;

                if (test->match (name, & out))
                        break;
        }

        LeaveCriticalSection (l_filterLock);

        if (out != 0) {
                * replace = (sockaddr_in *) out->ai_addr;
        } else
                * replace = 0;

        return test != 0;
}

/**@}*/
