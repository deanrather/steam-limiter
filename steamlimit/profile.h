#ifndef PROFILE_H
#define PROFILE_H               1

/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Simple classes to work with limiter profiles in the registry.
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

#define WIN32_LEAN_AND_MEAN     1
#include <windows.h>

/**
 * Simple class to wrap registry access on Windows, making it simpler to handle
 * releasing keys and to work with nested subkeys.
 */

class RegKey {
public:
        struct Binding {
                HKEY            m_key;
                const wchar_t * m_name;

                void            operator >>= (wchar_t * & string);
                void            operator >>= (unsigned long & value);
                void            operator >>= (ULONGLONG & value);
                void            operator >>= (bool & value);
                void            operator <<= (const wchar_t * string);
                void            operator <<= (unsigned long value);
                void            operator <<= (ULONGLONG value);
        };

private:
        HKEY            m_key;

public:
                        RegKey (const wchar_t * path);
                      ~ RegKey ();

        bool            open (const wchar_t * path, const RegKey * root = 0);
        Binding         operator [] (const wchar_t * name) const;
};


/**
 * For managing settings profiles.
 */

class Profile {
public:
        enum {
                g_noTraffic,
                g_home,
                g_away,
                g_custom,
                g_temp
        };

private:
        int             m_index;
        RegKey          m_reg;
        wchar_t       * m_country;
        wchar_t       * m_isp;
        wchar_t       * m_filter;
        wchar_t       * m_update;

        /* NOCOPY */    Profile (const Profile & copy);

        void            getValue (HWND window, UINT control, wchar_t * & value);
        void            setValue (HWND window, UINT control, const wchar_t * value);
        void            clean (void);

public:
                        Profile (int index, const RegKey * root);
                      ~ Profile ();

        const wchar_t * filter (void) const { return m_filter; }

        void            fromRegistry (const Profile * from = 0);
        void            fromWindow (HWND window);

        void            toRegistry (const Profile * to = 0);
        void            toWindow (HWND window, bool update = true);
};


/**@}*/
#endif  /* ! defined (PROFILE_H) */
