#ifndef INJECT_H
#define INJECT_H                1

/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Declare the core functions for calling into a DLL injected into a target
 * process.
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

/**
 * For globally recording the application's current path.
 */

extern  wchar_t       * g_appPath;

/**
 * Call into a filter DLL using a process ID, with the name and path of the
 * filter DLL being implicitly generated from the calling process.
 */

bool callFilterId (unsigned long processId, const char * entryPoint,
                   const wchar_t * param = 0, void * regRoot = 0,
                   const wchar_t * regPath = 0, const wchar_t * curDir = 0);

/**@}*/
#endif  /* ! defined (INJECT_H) */
