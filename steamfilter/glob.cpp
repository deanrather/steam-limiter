/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * Glob matching used in DNS control and the new general filter system.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2011-2012 Nigel Bree; All Rights Reserved.
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

#include "glob.h"

/**
 * Ultra-simple glob matcher, in classic UNIX v6 style.
 *
 * The only notable thing about this is that we're usually matching a simple
 * single-byte example string against a wide pattern, as a consequence of the
 * Steam client using gethostbyname ().
 */

bool globMatch (const char * example, const wchar_t * pattern,
                int slashMode) {
        if (example == 0)
                return false;

        wchar_t         ch;
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
                         *
                         * If that character is a backslash, it escapes the
                         * next pattern character.
                         */

                        if (ch == '\\' &&
                            (ch = * pattern) == 0) {
                                return false;
                        }

                        if (* example != ch)
                                return false;

                        ++ example;
                        continue;
                }

                /*
                 * Quickly recognise a trailing '*' as auto-success having got
                 * this far through the example - this makes sense as long as
                 * we either don't have a list of delimiter-type characters
                 * that '*' doesn't match - like '/' in the old UNIX shell,
                 * although that was more because path structure got parsed out
                 * of patterns first (and each path level was processed as part
                 * of directly expansion) - or because even if we do a trailing
                 * '*' can safely be a special case.
                 */

                ch = pattern [1];
                if (ch == 0)
                        return true;

                /*
                 * Do for our purposes should a '*' be able to match a '/' or
                 * not - one way of deciding is by whether there are any '/'
                 * characters in the rest of the pattern, or we can just say
                 * any interior '*' doesn't match a '/' but a trailing one does,
                 * or we can say that we can treat a star-slash combo as
                 * implying a not-match.
                 */

                bool            noSlash = slashMode == SLASH_NO_MATCH ? true :
                                          slashMode == SLASH_MATCH ? false :
                                          ch == '/' || ch == '.';

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
                         * Start with a recursive call just on the current
                         * example.
                         */

                        if (globMatch (example, pattern + 1, slashMode))
                                return true;

                        ch = * example;
                        if (ch == 0)
                                return false;

                        /*
                         * Before consuming a character, if we want to make '/'
                         * a special case we leave out of a '*' pattern we can
                         * do it here.
                         */

                        if (noSlash && ch == '/')
                                return false;

                        ++ example;
                }
        }

        return * example == 0;
}

/**@}*/
