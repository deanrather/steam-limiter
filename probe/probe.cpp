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

        if (* args == '"') {
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
 */

int checkHttp (SOCKET s) {
static  char            head [] = "HEAD /favicon.ico HTTP/1.0\n\n";
        int             result;
        int             length = strlen (head);
        result = send (s, head, length, 0);
        if (result < length)
                return 1;

        char            buf [1024];
        result = recv (s, buf, sizeof (buf), 0);
        return result < 5 ? 1 : 0;
}

/**
 * Probe for the indicated port at the given host.
 */

int probe (wchar_t * host, wchar_t * port) {

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

        WSADATA         wsaData;
        if (WSAStartup (MAKEWORD (2, 2), & wsaData) != 0)
                return 2;

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
                        result = checkHttp (s);
                break;

        default:
                break;
        }

        return result;
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

        int             result = probe (name, port);

        if (extra == 0)
                ExitProcess (result);

static  const char      text [] = "Probe result: ";
        WriteFile (err, text, strlen (text), & written, 0);

        char            buf [2] = { '0' + result };
        WriteFile (err, buf, 1, & written, 0);

        ExitProcess (result);
}
