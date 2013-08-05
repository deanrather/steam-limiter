/**@addtogroup Resource
 * @{@file
 *
 * Version information for the limiter
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

#define VER_MAJOR       0
#define VER_MINOR       7
#define VER_BUILD       0
#define VER_REV         0

#define VAL(x)                  #x
#define STR(x)                  VAL (x)

/**
 * File and product version information used by the steam-limiter resource
 * scripts.
 * @{
 */

#define VER_FILEVERSION         VER_MAJOR,VER_MINOR,VER_BUILD,VER_REV
#define VER_PRODUCTVERSION      VER_FILEVERSION

#define VER_PRODUCTNAME_STR     "SteamLimit"
#define VER_COMPANYNAME_STR     "Nigel Bree <nigel.bree@gmail.com>"
#define VER_WEBSITE_STR         "http://steam-limiter.googlecode.com"

#define VER_COPYRIGHT_STR       "Copyright 2011-2013 " VER_COMPANYNAME_STR

#define VER_FILEVERSION_STR     STR (VER_MAJOR) "." STR (VER_MINOR) "." STR (VER_BUILD) "." STR (VER_REV)
#define VER_PRODUCTVERSION_STR  VER_FILEVERSION_STR

/**@}*/
/**@}*/
