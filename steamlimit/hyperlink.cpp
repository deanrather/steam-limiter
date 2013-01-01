/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Simple class to convert a static dialog control into a hyperlink using
 * subclassing and a tiny bit of owner-draw.
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

#include "hyperlink.h"
#include <stdlib.h>

/**
 * Simple cliches for measuring arrays.
 */

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))

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
 * Basic message procedure which delegates to the object one.
 */

/* static */
LRESULT WINAPI Hyperlink :: proc (HWND window, UINT message, WPARAM wparam,
                                  LPARAM lparam) {
        Hyperlink     * link;
        link = (Hyperlink *) GetWindowLongPtr (window, GWL_USERDATA);
        return link->custom (window, message, wparam, lparam);
}

/**
 * Map from a window handle to the attached hyperlink object.
 */

/* static */
Hyperlink * Hyperlink :: at (HWND window, unsigned long item) {
        if (item != 0)
                window = GetDlgItem (window, item);
        if (window == 0)
                return 0;

        WNDPROC         proc;
        proc = (WNDPROC) GetWindowLongPtr (window, GWL_WNDPROC);
        if (proc != Hyperlink :: proc)
                return 0;

        return (Hyperlink *) GetWindowLongPtr (window, GWL_USERDATA);
}

/**
 * Simple constructor for the hyperlink to match the underlying control.
 */

Hyperlink :: Hyperlink (HWND window, WNDPROC proc) : m_proc (proc),
                m_tracking (false), m_focus (false), m_visited (false),
                m_underline (0), m_hand (0), m_text (0), m_link (0),
                m_bitmap (0), m_dc (0) {
        m_hand = LoadCursor (0, IDC_HAND);

        wchar_t         buf [128];
        size_t          length = GetWindowText (window, buf, ARRAY_LENGTH (buf));

        if (length == 0)
                return;

        /*
         * See if the window text can be split into a rendered caption and link.
         */

        wchar_t       * split = wcschr (buf, '|');
        if (split != 0) {
                * split = 0;
                ++ split;

                m_text = wcsdup (buf);
                m_link = wcsdup (split);
        } else
                m_text = m_link = wcsdup (buf);

        HFONT           base = (HFONT) SendMessage (window, WM_GETFONT, 0, 0);

        HDC             dc = GetDC (window);
        SelectObject (dc, base);

        SIZE            size;
        GetTextExtentPoint32 (dc, m_text, wcslen (m_text), & size);

        LOGFONTW        logFont;
        GetObject (base, sizeof (logFont), & logFont);
        logFont.lfUnderline = TRUE;
        m_underline = CreateFontIndirectW (& logFont);

        /*
         * Get the control's style bits to help do the resizing below in the
         * proper method according to the style.
         */

        unsigned long   style = GetWindowLong (window, GWL_STYLE);

        /*
         * Resize the underlying control to make it match the size of the
         * actual text. Width is more of an issue than height here.
         */

        RECT            rect;
        GetWindowRect (window, & rect);

        HWND            parent = GetParent (window);
        MapWindowPoints (0, parent, (LPPOINT) & rect, 2);

        /*
         * Add a few pixels of margin for a focus rectangle.
         */

        UINT            delta = rect.right + rect.left - size.cx;

        if ((style & BS_RIGHT) != 0) {
                rect.left += delta - 3;
                rect.right += 3;
        } else if ((style & BS_LEFT) != 0) {
                rect.left -= 3;
                rect.right -= delta - 3;
        } else {
                delta -= 6;
                rect.left += delta / 2;
                rect.right -= delta / 2;
        }

        m_rect.left = 0;
        m_rect.top = 0;
        m_rect.right = rect.right - rect.left;
        m_rect.bottom = rect.bottom - rect.top;

        SetWindowPos (window, 0, rect.left, rect.top, m_rect.right, m_rect.bottom,
                      SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);

        /*
         * Create an in-memory DC and bitmap matching the new control size so
         * we can render it double-buffered.
         */

        m_dc = CreateCompatibleDC (dc);
        m_bitmap = CreateCompatibleBitmap (dc, m_rect.right, m_rect.bottom);
        SelectObject (m_dc, m_bitmap);

        ReleaseDC (window, dc);
}

/**
 * Simple destructor to free some associated resources.
 */

Hyperlink :: ~ Hyperlink () {
        if (m_bitmap != 0)
                DeleteObject (m_bitmap);
        if (m_dc != 0)
                DeleteDC (m_dc);

        if (m_underline != 0)
                DeleteObject (m_underline);

        if (m_text != 0)
                free (m_text);
        if (m_link != 0 && m_link != m_text)
                free (m_link);
}

/**
 * Draw a focus rectangle around the link text.
 */

void Hyperlink :: focus (HWND window) {
        DrawFocusRect (m_dc, & m_rect);

        HDC             dc = GetDC (window);
        BitBlt (dc, 0, 0, m_rect.right, m_rect.bottom, m_dc, 0, 0, SRCCOPY);
        ReleaseDC (window, dc);
}

/**
 * Subclassed window procedure for the button.
 */

LRESULT Hyperlink :: custom (HWND window, UINT message, WPARAM wparam,
                             LPARAM lparam) {
        WNDPROC         proc = m_proc;
        switch (message) {
        case WM_DESTROY:
                SetWindowLong (window, GWL_WNDPROC, (ULONG_PTR) proc);
                delete this;
                break;

        case WM_MOUSEMOVE: {
                if (m_tracking)
                        break;

                TRACKMOUSEEVENT tme = { sizeof (tme) };
                tme.hwndTrack = window;
                tme.dwFlags = TME_LEAVE;
                TrackMouseEvent (& tme);

                m_tracking = true;
                InvalidateRect (window, 0, 0);
                break;
            }

        case WM_MOUSELEAVE:
                m_tracking = false;
                InvalidateRect (window, 0, 0);
                break;

        case WM_SETCURSOR:
                if (! m_tracking)
                        break;

                SetCursor (m_hand);
                return 0;

        case WM_SETFOCUS: {
                m_focus = true;
                focus (window);
                return 0;
            }

        case WM_KEYUP:
                if (wparam != VK_SPACE)
                        return 0;

        case WM_LBUTTONUP:
                m_visited = true;
                InvalidateRect (window, 0, 0);

                SendMessage (GetParent (window), WM_COMMAND,
                             MAKEWPARAM (GetDlgCtrlID (window), BN_CLICKED),
                             (LPARAM) window);
                return 0;

        case WM_KILLFOCUS:
                m_focus = false;
                focus (window);
                return 0;

        case WM_ERASEBKGND:
                return 0;

        case WM_PAINT: {
                /*
                 * Render the text to the backbuffer.
                 */

                SetBkColor (m_dc, GetSysColor (COLOR_MENU));
                SelectObject (m_dc, m_underline);
                SetTextColor (m_dc, m_tracking ? RGB (64, 64, 255):
                                    m_visited ? RGB (128, 0, 128) :
                                    RGB (0, 0, 204));

                HBRUSH          brush;
                brush = CreateSolidBrush (GetSysColor (COLOR_MENU));
                FillRect (m_dc, & m_rect, brush);

                if (m_focus)
                        DrawFocusRect (m_dc, & m_rect);

                DeleteObject (brush);

                DrawText (m_dc, m_text, wcslen (m_text), & m_rect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                /*
                 * Blit to the visible DC.
                 */


                PAINTSTRUCT     paint;
                HDC             dc = BeginPaint (window, & paint);
                BitBlt (dc, 0, 0, m_rect.right, m_rect.bottom, m_dc, 0, 0,
                        SRCCOPY);
                EndPaint (window, & paint);
                return 0;
            }
        }

        return CallWindowProc (proc, window, message, wparam, lparam);
}

/**
 * Attach the hyperlink object to an underlying static dialog control.
 */

/* static */
bool Hyperlink :: attach (HWND window, unsigned long id) {
        HWND            item = GetDlgItem (window, id);
        if (item == 0)
                return false;

        WNDPROC         proc;
        proc = (WNDPROC) GetWindowLongPtr (item, GWL_WNDPROC);

        Hyperlink     * link = new Hyperlink (item, proc);
        SetWindowLongPtr (item, GWL_USERDATA, (ULONG_PTR) link);
        SetWindowLongPtr (item, GWL_WNDPROC, (ULONG_PTR) & link->proc);

        /*
         * Set the owner-draw style on the underlying control; mostly this is
         * not necessary because the WM_PAINT handler takes care of things,
         * but when BN_SETSTATE is sent to draw the button in "pushed" state
         * it draws things itself without WM_PAINT (and overriding BN_SETSTATE
         * doesn't work well).
         *
         * The resulting WM_DRAWITEM doesn't need to do anything, so as long as
         * the parent is happy to ignore it, we're fine.
         */

        unsigned long   style = GetWindowLong (item, GWL_STYLE);
        SetWindowLong (item, GWL_STYLE, style | BS_OWNERDRAW);

        return true;
}

/**@}*/
