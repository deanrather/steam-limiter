/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Simple classes to work with limiter profiles in the registry.
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

#include "profile.h"
#include "resource.h"
#include <stdlib.h>

/**
 * Simple cliches for measuring arrays.
 */

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))

/**
 * Simple constructor to open a key; can always open () one later, too.
 */

RegKey :: RegKey (const wchar_t * path) : m_key (0) {
        if (path != 0)
                open (path);
}

/**
 * Simple destructor, just close any open key.
 */

RegKey :: ~ RegKey () {
        if (m_key != 0)
                RegCloseKey (m_key);
}

/**
 * Open a key, either in HKEY_CURRENT_USER or under the indicated parent.
 *
 * This uses RegCreateKeyEx () just because most of the time that's what an
 * application wanting to read data needs. The number of circumstances where
 * registry permissions need to be overridden is sufficiently small that this
 * should be the default.
 */

bool RegKey :: open (const wchar_t * path, const RegKey * root) {
        if (m_key != 0) {
                RegCloseKey (m_key);
                m_key = 0;
        }

        HKEY            key = root != 0 ? root->m_key : HKEY_CURRENT_USER;

        LSTATUS         result;
        result = RegCreateKeyExW (key, path, 0, 0, 0, KEY_ALL_ACCESS, 0,
                                  & m_key, 0);
        return result == ERROR_SUCCESS;
}

/**
 * Set up to retrieve a value.
 *
 * This is just some tedium needed to making working with C++ easier. This can
 * be done other ways such a iomanipulators and templates, but this is easy and
 * yields what I consider to be a perfectly fine syntax.
 */

RegKey :: Binding RegKey :: operator [] (const wchar_t * name) const {
        Binding         temp = { m_key, name };
        return temp;
}

/**
 * Assign a string-type value to a named value inside a key.
 */

void RegKey :: Binding :: operator <<= (const wchar_t * value) {
        if (value == 0) {
                RegDeleteValueW (m_key, m_name);
                return;
        }

        RegSetValueExW (m_key, m_name, 0, REG_SZ, (BYTE *) value,
                        (wcslen (value) + 1) * sizeof (wchar_t));
}

/**
 * Assign a numeric-type value to a named value inside a key.
 */

void RegKey :: Binding :: operator <<= (ULONGLONG value) {
        RegSetValueExW (m_key, m_name, 0, REG_QWORD, (BYTE *) & value,
                        sizeof (value));
}

/**
 * Assign a numeric-type value to a named value inside a key.
 */

void RegKey :: Binding :: operator <<= (unsigned long value) {
        RegSetValueExW (m_key, m_name, 0, REG_DWORD, (BYTE *) & value,
                        sizeof (value));
}

/**
 * Extract a boolean from a string-type named value.
 */

void RegKey :: Binding :: operator >>= (bool & value) {
        unsigned long   type;
        unsigned long   length;
        LSTATUS         result;
        result = RegQueryValueExW (m_key, m_name, 0, & type, 0, & length);

        value = result == ERROR_SUCCESS && type == REG_SZ;
}

/**
 * Read a simple string setting.
 */

void RegKey :: Binding :: operator >>= (wchar_t * & value) {
        if (value != 0)
                free (value);
        value = 0;

        unsigned long   type;
        unsigned long   length;
        LSTATUS         result;
        result = RegQueryValueExW (m_key, m_name, 0, & type, 0, & length);
        if (result != ERROR_SUCCESS || type != REG_SZ)
                return;

        value = (wchar_t *) malloc (length);
        result = RegQueryValueExW (m_key, m_name, 0, & type, (BYTE *) value,
                                   & length);
        if (result == ERROR_SUCCESS)
                return;

        free (value);
        value = 0;
}

/**
 * Retrieve a 64-bit value from the registry, defaulting to 0.
 */

void RegKey :: Binding :: operator >>= (unsigned long & value) {
        unsigned long   type;
        unsigned long   length = sizeof (value);

        LSTATUS         result;
        result = RegQueryValueExW (m_key, m_name, 0, & type, (BYTE *) & value,
                                   & length);
        if (result != ERROR_SUCCESS || length != sizeof (value))
                value = 0;
}

/**
 * Retrieve a 64-bit value from the registry, defaulting to 0.
 */

void RegKey :: Binding :: operator >>= (ULONGLONG & value) {
        unsigned long   type;
        unsigned long   length = sizeof (value);

        LSTATUS         result;
        result = RegQueryValueExW (m_key, m_name, 0, & type, (BYTE *) & value,
                                   & length);
        if (result != ERROR_SUCCESS || length != sizeof (value))
                value = 0;
}

/**
 * Simple constructor to select a profile index; if supplied, the registry root
 * will cause the profile to be loaded.
 */

Profile :: Profile (int index, const RegKey * root) :
                m_index (index), m_reg (0), m_country (0),
                m_isp (0), m_filter (0), m_update (false) {
        if (root == 0 || m_index == g_noTraffic)
                return;

        wchar_t         name [2] = { index - g_home + 'A' };
        m_reg.open (name, root);
        fromRegistry ();
}

/**
 * Simple destructor, clean up string memory.
 */

Profile :: ~ Profile () {
        clean ();
}

/**
 * Clean out any current settings.
 */

void Profile :: clean (void) {
        free (m_country);
        free (m_isp);
        free (m_filter);
        free (m_update);

        m_country = m_isp = m_filter = m_update = 0;
}

/**
 * Simple helper to set a value into a dialog control.
 *
 * This is a little tedious for checkbox support since for those, the window
 * text is the button label.
 */

void Profile :: getValue (HWND window, UINT control, wchar_t * & value) {
        HWND            ctrl = GetDlgItem (window, control);
        if (ctrl == 0)
                return;

        if (value != 0)
                free (value);
        value = 0;


        wchar_t         temp [1024];
        size_t          length;

        length = GetClassName (ctrl, temp, ARRAY_LENGTH (temp));

        if (wcscmp (temp, L"Button") == 0) {
                if (SendMessage (ctrl, BM_GETCHECK, 0, 0))
                        value = wcsdup (L"Y");
        } else {
                length = GetDlgItemText (window, control, temp, ARRAY_LENGTH (temp));
                if (length != 0)
                        value = wcsdup (temp);
        }
}

/**
 * Simple helper to assign a state to a dialog control.
 *
 * As a side-effect this can enable or disable controls; most controls are
 * disabled in most profiles, but for the custom profile we enable them.
 */

inline void Profile :: setValue (HWND window, UINT control, const wchar_t * value) {
        HWND            ctrl = GetDlgItem (window, control);
        if (ctrl == 0)
                return;

        wchar_t         temp [1024];
        size_t          length;

        length = GetClassName (ctrl, temp, ARRAY_LENGTH (temp));
        if (wcscmp (temp, L"Button") == 0) {
                SendMessage (ctrl, BM_SETCHECK, value != 0 ? BST_CHECKED : 0, 0);
        } else {
                SetDlgItemText (window, control, value);
        }

        bool            enable;

        if (control == IDC_UPDATE) {
                enable = m_index == g_home || m_index == g_away;

                EnableWindow (ctrl, enable);
        } else {
                enable = m_index == g_custom;
                SendMessage (ctrl, EM_SETREADONLY, ! enable, 0);
        }
}

/**
 * Load the profile index from the registry, or optionally from another
 * profile's registry index.
 *
 * The ability to load from another profile is just a handy way of managing a
 * temporary profile for external detection through the setfilter.js script.
 * In order to avoid registry modifications until the "OK" button in pressed
 * in filter dialogs, we can get the script to fetch into a temporary profile
 * in the registry then we can copy the data to where it needs to be.
 */

void Profile :: fromRegistry (const Profile * from) {
        const RegKey  & reg = from != 0 ? from->m_reg : m_reg;

        if (m_index == g_noTraffic) {
                /*
                 * The "No traffic" profile is now specific for Steam download
                 * traffic; nobbling the /depot/ URL prefix will stop the HTTP
                 * download system in its tracks regardless of what hosts are
                 * used.
                 */

                clean ();
                m_filter = wcsdup (L"*:27030=;/depot/*=");
                return;
        }

        reg [L"Country"] >>= m_country;
        reg [L"ISP"] >>= m_isp;
        reg [L"Filter"] >>= m_filter;
        reg [L"Update"] >>= m_update;
}

/**
 * Load the profile data int memory from a dialog window.
 */

void Profile :: fromWindow (HWND window) {
        getValue (window, IDC_COUNTRY, m_country);
        getValue (window, IDC_ISP, m_isp);
        getValue (window, IDC_FILTER, m_filter);
        getValue (window, IDC_UPDATE, m_update);
}

/**
 * Save the in-mmeory data to the profile's registry storage.
 */

void Profile :: toRegistry (const Profile * to) {
        const RegKey  & reg = to != 0 ? to->m_reg : m_reg;
        reg [L"Country"] <<= m_country;
        reg [L"ISP"] <<= m_isp;
        reg [L"Filter"] <<= m_filter;
        reg [L"Update"] <<= m_update;
}

/**
 * Send the current memory profile to a settings dialog.
 */

void Profile :: toWindow (HWND window, bool update) {
        setValue (window, IDC_COUNTRY, m_country);
        setValue (window, IDC_ISP, m_isp);
        setValue (window, IDC_FILTER, m_filter);

        /*
         * When transferring information from a temporary profile, leave the
         * state of the "update" button alone.
         */

        if (update)
                setValue (window, IDC_UPDATE, m_update);
}

/**@}*/
