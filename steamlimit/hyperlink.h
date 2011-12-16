#ifndef HYPERLINK_H
#define HYPERLINK_H             1

/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Simple class to convert a static dialog control into a hyperlink using
 * subclassing and a tiny bit of owner-draw.
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
 * Helper for subclassing a standard control; in particular we want to subclass
 * a button control to give the effect of being owner-draw so it can be made
 * into a hyperlink control.
 *
 * Unlike MFC, we'll just do the simple kind of Win32 subclassing with the user
 * data object pointing to the subclass context.
 */

class Hyperlink {
private:
        WNDPROC         m_proc;
        bool            m_tracking;
        bool            m_focus;
        bool            m_visited;
        HFONT           m_base;
        HFONT           m_underline;
        HCURSOR         m_hand;
        wchar_t       * m_text;
        wchar_t       * m_link;
        HBITMAP         m_bitmap;
        HDC             m_dc;
        RECT            m_rect;

        LRESULT         custom (HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam);
static  LRESULT WINAPI  proc (HWND wnd, UINT message, WPARAM wparam,
                              LPARAM lparam);
        void            focus (HWND window);
        void            init (HWND window);

                        Hyperlink (HWND window, WNDPROC proc);
        /* NOCOPY */    Hyperlink (Hyperlink &);

public:
                      ~ Hyperlink ();

static  Hyperlink     * at (HWND window, unsigned long item = 0);
static  bool            attach (HWND window, unsigned long id);

const wchar_t * link (void) const { return m_link; }
};

/**@}*/
#endif  /* ! defined (HYPERLINK_H) */
