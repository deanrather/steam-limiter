#ifndef REPLACE_H
#define REPLACE_H               1

/**@addtogroup Filter Steam limiter filter hook DLL.
 * @{@file
 *
 * This declares the structures and functions for replacing a document in a
 * client application's stream of HTTP requests.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2013 Nigel Bree; All Rights Reserved.
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

struct Replacement;

#include <winsock2.h>

typedef void          * ReplaceHKEY;

void            g_initReplacement (ReplaceHKEY key, const wchar_t * regPath);
void            g_unloadReplacement (void);

void            g_addEventHandle (SOCKET handle, WSAEVENT event);
void            g_removeTracking (SOCKET handle);

void            g_replacementCache (const wchar_t * name);
bool            g_addReplacement (SOCKET handle, const char * name,
                                  const char * url);
Replacement   * g_findReplacement (SOCKET handle);
bool            g_consumeReplacement (Replacement * item, unsigned long length,
                                      void * buf, unsigned long * copied);

/**@}*/
#endif  /*! defined (REPLACE_H) */
