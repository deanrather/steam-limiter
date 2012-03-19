/**@addtogroup Probe Test whether a given port on a host is connectable.
 * @{@file
 *
 * This application attempts to connect to a server and port, and just returns
 * the status as a result to the invoker.
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

#include <winsock2.h>
#include <ws2tcpip.h>

/**
 * Cliche for returning array lengths.
 */

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof ((a) [0]))

/**
 * Simple command-line argument extraction, about as unsophisticated as it can
 * possibly get.
 */

wchar_t * split (wchar_t * args) {
        if (args == 0)
                return args;

        /*
         * The argument is quoted (which is typical of the first argument, the
         * program name, because of the need to avoid problems with spaces in
         * the path), in which case we skip to the ending quote first.
         */

        wchar_t       * quoted = 0;
        if (* args == '"') {
                quoted = args;
                for (;;) {
                        ++ args;
                        wchar_t         ch = * args;
                        if (ch == '"')
                                break;

                        if (ch == 0)
                                return 0;
                }
        }

        /*
         * Split at the next space.
         */

        for (;;) {
                wchar_t         ch = * args;
                if (ch == ' ')
                        break;

                if (ch == 0)
                        return 0;
                ++ args;
        }

        * args = 0;
        ++ args;

        /*
         * If the arguments start with quotes, strip the quotes (this isn't a
         * completely generic thing to do, but it fits our purposes).
         */

        if (quoted) {
                wchar_t       * from = quoted;
                wchar_t         ch;
                while ((ch = * ++ quoted) != '"')
                        * from ++ = ch;

                while (ch != 0) {
                        ch = * ++ quoted;
                        * from ++ = ch;
                }

                * from = ch;
        }

        /*
         * If there are additional spaces, consume them.
         */

        while (* args == ' ')
                ++ args;

        return args;
}

/**
 * If we're asked to probe for an NTTP port, then sometimes we have to deal
 * with local proxies.
 *
 * In this case, we actually try and read from the socket to at least get the
 * server's initial hello. For the annoying Avast! proxy, that at least does
 * not get sent until the real target responds to the proxy, and if the proxy
 * connection doesn't work (after 5-6 seconds, since it tries the TLS version
 * of the port even if we connected plain-text) then it spits a 400 out.
 */

int checkNntp (SOCKET s) {
        char            buf [128];
        int             result;
        result = recv (s, buf, sizeof (buf), 0);
        if (result < 5)
                return 1;

        /*
         * Various tedious socket-isms can apply, such as the bytes for the
         * initial response code trickling in over time. Let's not worry
         * about that, just deal with the basics.
         */

        void          * end = memchr (buf, ' ', result);
        if (end == 0)
                return 1;

        return memcmp (buf, "400", 3) == 0 ? 1 : 0;
}

/**
 * If we're asked to probe for an HTTP port, then we need to avoid problems
 * with local proxies.
 *
 * Unlike NNTP, the HTTP protocol is client-driven; as it turns out the way the
 * crappy Avast! proxy works is that it'll unilaterally close the connection if
 * it can't reach the real intended target, but in order to have this work for
 * real targets it pays to request a resource. The safest thing to ask for seems
 * to be favicon.ico - it's something lots of browsers request anyway and it's
 * almost always a small image, so we shouldn't clog up logs with requests for
 * 404 resources or get elaborate 404 response pages back.
 *
 * It turns out that the Avast! proxy has some other exciting misbehaviours; it
 * will sometimes (but not always) when a connection fails return an "HTTP/1.1
 * 200 OK" status with some fixed bogus fields, one of which is a "Refresh: 1;"
 * to re-fetch the target URL.
 */

int checkHttp (SOCKET s, HANDLE show) {
static  char            head [] = "HEAD /favicon.ico HTTP/1.0\n\n";
        int             result;
        int             length = strlen (head);
        result = send (s, head, length, 0);
        if (result < length)
                return 1;

        char            buf [1024];
        result = recv (s, buf, sizeof (buf) - 1, 0);

        /*
         * Show the HTTP response, for debugging. I'll keep this in as long as
         * it doesn't cost me any compile-time space. I started out aiming to
         * keep this around 4kb, and now that the traceroute code is in the
         * aim is to keep it below 8kb.
         */

        if (result > 0 && show > 0) {
                unsigned long   written = 0;
                WriteFile (show, buf, result, & written, 0);
        }

        /*
         * Normally we wouldn't care what the actual response text was, but to
         * deal with the fake response sometimes returned from Avast! I have to
         * recognize and suppress it.
         */

        if (result > 0) {
                buf [result] = 0;
                if (strstr (buf, "\nRefresh: 1;") != 0)
                        return 1;
        }

        return result < 5 ? 1 : 0;
}

/**
 * Probe for the indicated port at the given host.
 */

int probe (wchar_t * host, wchar_t * port, HANDLE show) {
        /*
         * Detect the presence of the Avast! virus scanner; it includes a set
         * of proxy-type firewalls that are somewhat tedious to deal with, and
         * in the case of NNTP mean that NNTP connections go through their
         * proxy; so, our connect will succeed (being locally looped back) and
         * send us nothing while the proxy module slowly decides whether it can
         * or can't connect to the real target over TLS or plain-text NNTP.
         *
         * So, if Avast! is present we can choose to either try and read from
         * the socket once it connects (so we can get the response code, which
         * will generally be 400 once the Avast proxy fails), or we can forget
         * the whole probing process because that introduces too much delay.
         */

        HMODULE         avast = GetModuleHandle (L"snxhk.dll");

        SOCKET          s;
        s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == SOCKET_ERROR)
                return 2;

        sockaddr_in     any;
        any.sin_family = AF_INET;
        any.sin_port = 0;
        any.sin_addr.S_un.S_addr = INADDR_ANY;
        if (bind (s, (sockaddr *) & any, sizeof (any)) != 0)
                return 2;

        /*
         * Look up the hostname and convert the port number into numeric form,
         * all handily in one function.
         */

        ADDRINFOW     * address;
        if (GetAddrInfoW (host, port, 0, & address) != 0)
                return 2;

        /*
         * Ensure that we only connect via IPv4, having made an IPv4 socket
         * already (yes, I could do things in a different order, but for my
         * purposes here with Steam I care about IPv4 only for now since they
         * are IPv4-only).
         */

        while (address->ai_addr->sa_family != AF_INET)
                if ((address = address->ai_next) == 0)
                        return 2;

        /*
         * Just test whether the port is open or not.
         */

        int             result;
        result = connect (s, address->ai_addr, address->ai_addrlen) ? 1 : 0;

        /*
         * Decide whether to actually wait for data, if we're dealing with the
         * Avast! proxy being present on a system.
         */

        sockaddr_in   * addr = (sockaddr_in *) address->ai_addr;
        switch (htons (addr->sin_port)) {
        case 119:
                if (avast && result == 0)
                        result = checkNntp (s);
                break;

        case 80:
                if (avast && result == 0)
                        result = checkHttp (s, show);
                break;

        default:
                break;
        }

        return result;
}

#include <iphlpapi.h>
#include <icmpapi.h>
#include "../steamfilter/glob.h"

/*
 * As an alternative to probing for a host, do a traceroute and permit glob
 * matches against the hostnames.
 *
 * In principle one could just script a traceroute, but that's a bit slow and
 * rather than writing a regex against the output it seems better to have a
 * more direct match available.
 */

int trace (wchar_t * host, wchar_t * pattern, HANDLE err) {
        HANDLE          icmp = IcmpCreateFile ();
        if (icmp == INVALID_HANDLE_VALUE)
                return 2;

        /*
         * Resolve an IPv4 hostname.
         */

        ADDRINFOW     * address;
        if (GetAddrInfoW (host, 0, 0, & address) != 0)
                return 2;

        /*
         * Ensure that we only connect via IPv4, having made an IPv4 socket
         * already (yes, I could do things in a different order, but for my
         * purposes here with Steam I care about IPv4 only for now since they
         * are IPv4-only).
         */

        while (address->ai_addr->sa_family != AF_INET)
                if ((address = address->ai_next) == 0)
                        return 2;

        sockaddr_in   * addr = (sockaddr_in *) address->ai_addr;
        IPAddr          dest = addr->sin_addr.s_addr;

        /*
         * The timeouts in this loop are tighter than they are in general kinds
         * of traceroute applications since we are generally probing for things
         * near to the origin system and with latencies in the <50ms bracket.
         *
         * We'll also only use a relatively short TTL for the echo requests as
         * we're matching the first host with a DNS name. Also, some ISPs block
         * ICMP echo on their Steam servers (e.g. TelstraClear, who also keep
         * port 80 firewalled) so there's no point searching too hard since the
         * route will stall after only 2 or so hops.
         */

        unsigned short  ttl = 1;
        for (; ttl < 8 ; ++ ttl) {
                unsigned char   buf [128];

                /*
                 * Part the first; send an echo request.
                 */

                IP_OPTION_INFORMATION info = { ttl };

                DWORD           echo;
                echo = IcmpSendEcho (icmp, dest, 0, 0, & info, buf,
                                     sizeof (buf), 50);
                if (echo < 1) {
                        /*
                         * Allow one retry, "just in case".
                         */

                        echo = IcmpSendEcho (icmp, dest, 0, 0, & info, buf,
                                             sizeof (buf), 50);
                        if (echo < 1)
                                continue;
                }

                /*
                 * We expect to see IP_TTL_EXPIRED_TRANSIT since we set the TTL
                 * to find the intermediate systems.
                 */

                ICMP_ECHO_REPLY * reply = (ICMP_ECHO_REPLY *) buf;
                if (reply->Status != IP_TTL_EXPIRED_TRANSIT && reply->Status != 0)
                        return 1;

                /* 
                 * Part the second; protocol-independent reverse name lookup.
                 */

                sockaddr_in     find = { AF_INET };
                find.sin_addr.s_addr = reply->Address;
                find.sin_port = 0;

                char            name [128];
                char            port [20];
                unsigned long   error;
                error = getnameinfo ((SOCKADDR *) & find, sizeof (find),
                                     name, ARRAY_LENGTH (name),
                                     0, 0, NI_NAMEREQD);

                if (error != 0)
                        continue;

                /*
                 * If we're given a handle to write to, print the name we found.
                 */

                unsigned long   written = 0;
                WriteFile (err, name, strlen (name), & written, 0);
                WriteFile (err, "\r\n", 2, & written, 0);

                /*
                 * If the status is 0, then we've hit the target host and that
                 * means we should stop and return 1.
                 */

                if (reply->Status == 0 || reply->Address == dest)
                        break;

                /*
                 * If the pattern is empty, we're just printing results.
                 */

                if (pattern == 0 || * pattern == 0)
                        continue;

                /*
                 * We have a name, now we can glob-match it. If we see the
                 * desired pattern, we win. If we don't, we bail; the first
                 * name we resolve wins.
                 */

                return globMatch (name, pattern) ? 0 : 1;
        }

        return 1;
}

/**
 * This is intended as a "naked" WinMain without the Visual C++ run-time
 * at all (not just avoiding the broken locale machinery).
 */

int CALLBACK myWinMain (void) {
        HANDLE          err = GetStdHandle (STD_ERROR_HANDLE);
        unsigned long   written = 0;

        /*
         * Since we're not using the regular C machinery, get and split the
         * command line by hand. The CommandLineToArgvW () routine would do the
         * normal conversion to C style for us, but that depends on SHELL32.DLL
         * and we shouldn't need it.
         */

        wchar_t       * base = GetCommandLineW ();
        wchar_t       * name = split (base);
        wchar_t       * port = split (name);
        wchar_t       * extra = split (port);
        if (port == 0)
                return 2;

        WSADATA         wsaData;
        if (WSAStartup (MAKEWORD (2, 2), & wsaData) != 0)
                return 2;

        int             result;
        if (wcscmp (port, L"icmp") == 0) {
                wchar_t       * find = extra;
                extra = split (find);
                if (find == 0 || * find == 0) {
                        /*
                         * If there is no third argument, just print the names
                         * of the first few systems we find.
                         */

                        find = 0;
                } else if (extra == 0)
                        err = INVALID_HANDLE_VALUE;

                result = trace (name, find, err);
        } else {
                if (extra == 0)
                        err = INVALID_HANDLE_VALUE;

                result = probe (name, port, err);
        }

        if (extra == 0)
                ExitProcess (result);

static  const char      text [] = "Probe result: ";
        WriteFile (err, text, strlen (text), & written, 0);

        char            buf [2] = { '0' + result };
        WriteFile (err, buf, 1, & written, 0);

        ExitProcess (result);
}
