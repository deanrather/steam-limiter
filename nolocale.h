/**@addtogroup Common
 * @{@file
 *
 * Disable the VS2010 version of the C++ runtime locale support.
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
 *
 * This exists simply to reduce the size of the monitor and hook executables,
 * since the locale machines in Visual C++ is worthless and takes up about 14kb
 * of space we can't normally reclaim.
 *
 * These are specific to VS2010; I've written more versatile versions that go
 * back to very old VC editions but this VS2010-only one will do for now.
 */

#define WIN32_LEAN_AND_MEAN     1
#include <windows.h>

extern "C" {
      
/**@cond internal */

unsigned long __flsindex = FLS_OUT_OF_INDEXES;

void * gpFlsAlloc = 0;
void * gpFlsGetValue = 0;
void * gpFlsSetValue = 0;
void * gpFlsFree = 0;

void __cdecl _freeptd (void *) {
}

void __cdecl _initptd (void *, void *) {
}

void * __cdecl _getptd (void) {
        return 0;
}
void * __cdecl _getptd_noexit (void) {
        return 0;
}

/**
 * Fake FLS allocator/set/get routines.
 */

DWORD WINAPI __crtFlsAlloc (PFLS_CALLBACK_FUNCTION) {
        return FLS_OUT_OF_INDEXES;
}

PVOID WINAPI __crtFlsGetValue (DWORD) {
        return 0;
}

BOOL WINAPI __crtFlsSetValue (DWORD, PVOID) {
        return TRUE;
}

BOOL WINAPI __crtFlsFree (DWORD) {
        return TRUE;
}

void __cdecl _init_pointers (void);
void __cdecl _mtinitlocks (void);

void __cdecl _mtinit (void) {
        _init_pointers ();
        _mtinitlocks ();

        HMODULE         kernel = GetModuleHandleW (L"kernel32.dll");
        if (kernel == 0)
                return;

        gpFlsAlloc = EncodePointer (__crtFlsAlloc);
        gpFlsGetValue = EncodePointer (__crtFlsGetValue);
        gpFlsSetValue = EncodePointer (__crtFlsSetValue);
        gpFlsFree = EncodePointer (__crtFlsFree);
}

void __cdecl _mtdeletelocks (void);

void __cdecl _mtterm (void) {
        _mtdeletelocks ();
}

void * __cdecl __set_flsgetvalue (void) {
        return 0;
}

void * _encoded_null (void) {
        return EncodePointer (0);
}

void __cdecl __initmbctable (void) {
}

void * __pPurecall = 0;

void _initp_misc_purevirt (void * encoded_null) {
        __pPurecall = encoded_null;
}

int __cdecl _setargv (void) {
        return 0;
}

int __cdecl _setenvp (void) {
        return 0;
}

/**@endcond*/

}
