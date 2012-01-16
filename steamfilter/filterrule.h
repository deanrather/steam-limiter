#ifndef FILTERRULE_H
#define FILTERRULE_H            1

/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * This declares a data structure and parser for rules to use in connection
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

/**
 * To make the filter system more generic, I'll define a text language for
 * specifying more generally what the hooks can match and what to do in
 * response to a match.
 *
 * The replacement selection can be a list of potential items, one of which 
 * can be selected on a round-robin basis, allowing multiple targest to be
 * load-balanced. Filters and targets can both optionally contain ports as
 * well as hostnames or IP addresses, allowing more general rewriting of the
 * target of a connection attempt (including redirection of remote connects
 * through local proxies).
 *
 * The intention here is partly to enable wider utility for the Steam limiter
 * such as for Australian users (where ISPs have engaged in a small amount of
 * cooperation and peering of their unmetered steam servers, so the filter can
 * redirect to a list to permit faster parallel downloading), but also to make
 * the limiter more useful for business purposes.
 *
 * In particular, one of the perennial annoyances in building network services
 * is creating local test harnesses such as for continuous build purposes, and
 * similarly for fault injection to validate error-management paths in code.
 * Using the filter system and rulebase should make it much easier to create
 * such tests for use in build and QA automation.
 */

struct addrinfo;
struct sockaddr_in;
class FilterRules;

/**
 * Data structure representing parsed filters.
 *
 * In the early versions, I just did a simple binary match on a single IP
 * address; to make this completely general, it's better to do a pattern kind
 * of match, and to generalize the replacement concept a little so that there
 * is a simple filter specification syntax I can use both for connections and
 * for DNS lookups in one.
 *
 * Another concept here is that I can not only replace the target IP, but the
 * port as well, and multiple rewrite targets are rotated around.
 */

class FilterRule {
        friend class FilterRules;

private:
        wchar_t       * m_pattern;
        bool            m_hasPort;
        unsigned short  m_port;
        char          * m_rewrite;
        addrinfo      * m_replace;
        addrinfo      * m_nextReplace;
        FilterRule    * m_next;

static  const wchar_t * lookahead (const wchar_t * from, const wchar_t * to,
                                   wchar_t ch);
static  wchar_t       * unescape (wchar_t * dest, size_t length,
                                  const wchar_t * from, const wchar_t * to);
static  wchar_t       * wcsdup (const wchar_t * from, const wchar_t * to);
static  wchar_t       * wcscatdup (const wchar_t * left, const wchar_t * middle,
                                   const wchar_t * right);
static  char          * urldup (const wchar_t * from, const wchar_t * to);

        void            freeInfo (addrinfo * info);

        const wchar_t * hasPort (const wchar_t * from, const wchar_t * to,
                                 unsigned short & port);
        bool            parseReplace (const wchar_t * from, const wchar_t * to,
                                      addrinfo * & link);
        bool            parseRule (const wchar_t * from, const wchar_t * to);

public:
static  bool            installFilters (wchar_t * str);

        bool            match (const char * example, addrinfo ** replace);
        bool            match (const char * example, const char ** replace);

                        FilterRule ();
                      ~ FilterRule ();
};

/**
 * Represent a collection of filter rules.
 */

class FilterRules {
private:
        FilterRule    * m_head;
        FilterRule    * m_tail;
        wchar_t       * m_pending;

        unsigned short  m_defaultPort;

static  void            freeRules (FilterRule * head);

        bool            parse (const wchar_t * from, const wchar_t * to,
                               FilterRule * & head, FilterRule * & tail);

public:
                        FilterRules (unsigned short defaultPort = 0);
                      ~ FilterRules ();

        bool            append (const wchar_t * rules);
        bool            install (const wchar_t * rules);

        bool            match (const sockaddr_in * name, void * module,
                               sockaddr_in ** replace);
        bool            match (const char * name,
                               sockaddr_in ** replace);
        bool            match (const char * name,
                               const char ** replace);
};

/**@}*/
#endif  /* ! defined (FILTERRULE_H) */
