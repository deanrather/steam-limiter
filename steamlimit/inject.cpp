/**@addtogroup Monitor Steam limiter monitor application.
 * @{@file
 *
 * Define the core functions for calling into a DLL injected into a target
 * process.
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
#include <stddef.h>
#include "inject.h"

/**
 * Simple cliches for measuring arrays.
 */

#define ARRAY_LENGTH(a)         (sizeof (a) / sizeof (* a))
#define ARRAY_END(a)            ((a) + ARRAY_LENGTH (a))

/**
 * Code array containing the initial shim we'll use to bootstrap in our filter
 * DLL to the target process.
 *
 * This is page-sized since VirtualAllocEx () works in page-sized units, and to
 * return results from the called DLL it's handy to just copy the entire block
 * in and out.
 */

unsigned char   codeBytes [4096];

/**
 * Write a 32-bit value into the output in Intel byte order.
 */

unsigned char * writeLong (void * dest, unsigned long value) {
        unsigned char * out = (unsigned char *) dest;
        * out ++ = value & 0xFF;
        value >>= 8;
        * out  ++ = value & 0xFF;
        value >>= 8;
        * out  ++ = value & 0xFF;
        value >>= 8;
        * out  ++ = value & 0xFF;
        return out;
}

/**
 * Write a pointer into the output in Intel byte order.
 *
 * Since the target process is currently only 32-bit, assume that for now.
 */

inline unsigned char * writePointer (void * dest, void * ptr) {
        return writeLong (dest, (unsigned long) ptr);
}

/**
 * Write a string into the output. The output should be aligned properly.
 */

unsigned char * writeString (unsigned char * dest, const wchar_t * src) {
        unsigned long   length = (wcslen (src) + 1) * sizeof (wchar_t);
        memcpy (dest, src, length);
        return dest + length;
}

/**
 * Write a string into the output.
 */

unsigned char * writeString (unsigned char * dest, const char * src) {
        unsigned long   length = strlen (src) + 1;
        memcpy (dest, src, length);
        return dest + length;
}

/**
 * Various code-building macros we use to make the shim
 */

#define PUSH_EAX        0x50
#define PUSH_EBX        0x53
#define POP_EAX         0x58
#define JE              0x74

#define MOV_RM          0x89
#define ESI_OFFSET_EAX  0x46
#define EBP_ESP         0xE5
#define ESP_EBP         0xEC

#define MOV_REG         0x8B
#define EBX_ESI_OFFSET  0x5E
#define EAX_ESI_OFFSET  ESI_OFFSET_EAX

#define LEA             0x8D
#define ESI_MEM         0x35
#define RET             0xC3

#define INDIRECT        0xFF
#define PUSH_ESI_OFFSET 0x76
#define CALL_ESI_OFFSET 0x56

#define OR_REG          0x0B
#define XOR_REG         0x31

#define EAX_EAX         0xC0

#define LEA_ESI_ADDRESS(p, x) do { \
                * p ++ = LEA; \
                * p ++ = ESI_MEM; \
                p = writePointer (p, x); \
        } while (0)

#define LEA_EBX_ESI_AT(p, o) do { \
                * p ++ = LEA; \
                * p ++ = EBX_ESI_OFFSET; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define MOV_ESI_AT_EAX(p, o) do { \
                * p ++ = MOV_RM; \
                * p ++ = ESI_OFFSET_EAX; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define MOV_EAX_ESI_AT(p, o) do { \
                * p ++ = MOV_REG; \
                * p ++ = EAX_ESI_OFFSET; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define MOV_EBP_ESP(p) do { \
                * p ++ = MOV_RM; \
                * p ++ = EBP_ESP; \
        } while (0)

#define MOV_ESP_EBP(p) do { \
                * p ++ = MOV_RM; \
                * p ++ = ESP_EBP; \
        } while (0)

#define MOV_EBX_ESI_AT(p, o) do { \
                * p ++ = MOV_REG; \
                * p ++ = EBX_ESI_OFFSET; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define PUSH_ESI_AT(p, o) do { \
                * p ++ = INDIRECT; \
                * p ++ = PUSH_ESI_OFFSET; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define CALL_ESI_SIZE   3
#define CALL_ESI_AT(p, o) do { \
                * p ++ = INDIRECT; \
                * p ++ = CALL_ESI_OFFSET; \
                * p ++ = (unsigned char) (o); \
        } while (0)

#define OR_EAX_EAX(p) do { \
                * p ++ = OR_REG; \
                * p ++ = EAX_EAX; \
        } while (0)

#define XOR_EAX_EAX(p) do { \
                * p ++ = XOR_REG; \
                * p ++ = EAX_EAX; \
        } while (0)

#define JE_(p, o) do { \
                * p ++ = JE; \
                * p ++ = (unsigned char) (o); \
        } while (0)

/**
 * Inject our filter DLL into the steam process.
 *
 * This is done using fairly normal DLL injection techniques. One of the few
 * things I don't do to keep this clean is try and adapt to different load
 * addresses of KERNEL32.DLL; at some point Windows may introduce a form of
 * address-space randomization which will even go that far, in which case I'd
 * have to work slightly harder to bootstrap the injected code, but for now the
 * classic techniques should do.
 */

unsigned long injectFilter (HANDLE steam, unsigned char * mem,
                            const wchar_t * path, const char * entryName,
                            const wchar_t * paramString = 0) {
        /*
         * The first two entries in the codeBytes will be the addresses of the
         * LoadLibrary and GetProcAddress functions which will bootstrap in our
         * monitor DLL.
         *
         * The point of doing things this way is that it's the easiest way to
         * avoid having to use relative addressing for the calls into the
         * routines.
         *
         * Note that the assumption that these things will match in the source
         * and destination processes will fail severely if the Win2 Application
         * Verifier is used on us, as that'll revector GetProcAddress to the
         * verifier shim which won't exist in the Steam process.
         */

        HMODULE         kernel = GetModuleHandleA ("KERNEL32.DLL");
        if (kernel == 0)
                return 0;

        /*
         * Reserve space for a pointer to the parameters to be passed to the
         * invoked function. This uses a pointer rather than the data so that
         * we can reasonably assure ourselves that everything fits within the
         * 128-byte short offset encoding we use below.
         */

        struct ParamBlock {
                FARPROC         loadLib;
                FARPROC         gmh;
                FARPROC         gpa;
                FARPROC         freeLib;
                wchar_t       * param;
                wchar_t       * path;
                void          * result;
                size_t          resultSize;
                HMODULE         loadedLibrary;
                unsigned char * entryName;
                unsigned char * entryPoint;
        };

        ParamBlock    * params = (ParamBlock *) codeBytes;

        /*
         * Always clear out any stale values from a previous cycle, now that we
         * copy back the entire parameter block from a call; we don't want any
         * stale data in the new context.
         */

        memset (params, 0, sizeof (codeBytes));

        params->loadLib = GetProcAddress (kernel, "LoadLibraryW");
        params->gmh = GetProcAddress (kernel, "GetModuleHandleW");
        params->gpa = GetProcAddress (kernel, "GetProcAddress");
        params->freeLib = GetProcAddress (kernel, "FreeLibrary");

        /*
         * Write the parameter string, and then backpatch a pointer to it
         * into the structure slot for it.
         */

        unsigned char * dest = (unsigned char *) (params + 1);
        unsigned char * start;

        if (paramString != 0) {
                dest = writeString (start = dest, paramString);
                writePointer (& params->param, mem + (start - codeBytes));
        }

        /*
         * Write the actual path string, and backpatch a pointer to it; also
         * find the last backslash in the path string, and use that to work out
         * a pointer to the last part of the path to see if we want to use
         * GetModuleHandle () instead.
         *
         * Using GetModuleHandle () is a nice trick if there might potentially
         * be another copy of our injected DLL (possibly an older version, for
         * instance) floating around blocking a normal load attempt.
         */

        bool            getModule = wcschr (path, L'\\') == 0;
        dest = writeString (start = dest, path);
        writePointer (& params->path, mem + (start - codeBytes));

        /*
         * Now write out the name of the entry point we want to use; this is
         * in plain ASCII because GetProcAddress works that way.
         *
         * We allow this to be empty for the same reason we have support for
         * GetModuleHandle (). It allows us to express the desire to in effect
         * force-unload a module by decrementing its reference count.
         */

        if (entryName != 0) {
                dest = writeString (start = dest, entryName);
                writePointer (& params->entryName, mem + (start - codeBytes));
        }

        /*
         * Starting here we build the actual x86 loader code.
         */

        dest += (dest - codeBytes) & 1;
        unsigned long   codeOffset = dest - codeBytes;

        /*
         * Obtain a base pointer to our code frame which we can use to access
         * the values we've stashed in it above.
         */

        LEA_ESI_ADDRESS (dest, mem);

        /*
         * myLib = LoadLibraryW (L"steamfilter.dll");
         */

        if (getModule) {
                PUSH_ESI_AT (dest, offsetof (ParamBlock, path));
                CALL_ESI_AT (dest, offsetof (ParamBlock, gmh));

                if (entryName == 0)
                        MOV_ESI_AT_EAX (dest, offsetof (ParamBlock, loadedLibrary));
        } else {
                PUSH_ESI_AT (dest, offsetof (ParamBlock, path));
                CALL_ESI_AT (dest, offsetof (ParamBlock, loadLib));

                MOV_ESI_AT_EAX (dest, offsetof (ParamBlock, loadedLibrary));
        }

        if (entryName != 0) {
                /*
                 * myFunc = GetProcAddress (mylib, "SteamFilter");
                 */

                PUSH_ESI_AT (dest, offsetof (ParamBlock, entryName));
                * dest ++ = PUSH_EAX;
                CALL_ESI_AT (dest, offsetof (ParamBlock, gpa));
                MOV_ESI_AT_EAX (dest, offsetof (ParamBlock, entryPoint));

                /*
                 * if (myFunc != 0)
                 *   (* myFunc) (param, & result, & resultSize);
                 *
                 * Note that we guard this by saving ESP in EBP so we're robust if the
                 * calling convention or parameter count don't match. The function is
                 * passed the address of the incoming string and pointer to free space
                 * in the shim it can write some output to.
                 */

                MOV_EBP_ESP (dest);
                LEA_EBX_ESI_AT (dest, offsetof (ParamBlock, resultSize));
                * dest ++ = PUSH_EBX;
                PUSH_ESI_AT (dest, offsetof (ParamBlock, result));
                PUSH_ESI_AT (dest, offsetof (ParamBlock, param));

                MOV_EAX_ESI_AT (dest, offsetof (ParamBlock, entryPoint));
                OR_EAX_EAX (dest);
                JE_ (dest, CALL_ESI_SIZE);

                CALL_ESI_AT (dest, offsetof (ParamBlock, entryPoint));

                MOV_ESP_EBP (dest);
        } else
                XOR_EAX_EAX (dest);

        /*
         * if (myLib != 0)
         *    FreeLibrary (myLib);
         * return;
         */

        * dest ++ = PUSH_EAX;
        MOV_EAX_ESI_AT (dest, offsetof (ParamBlock, loadedLibrary));
        OR_EAX_EAX (dest);
        JE_ (dest, CALL_ESI_SIZE + 1);

        * dest ++ = PUSH_EAX;
        CALL_ESI_AT (dest, offsetof (ParamBlock, freeLib));

        * dest ++ = POP_EAX;
        * dest ++ = RET;

        /*
         * Now that we've build the code, align things and what's left in the
         * code page is potential return result room.
         */

        size_t          offset = (dest - codeBytes + 15) & ~ 15;

        writePointer (& params->result, mem + offset);
        writeLong (& params->resultSize, sizeof (codeBytes) - offset);

        /*
         * Now write the shim into the target process.
         */

        unsigned long   wrote;
        if (! WriteProcessMemory (steam, mem, codeBytes, sizeof (codeBytes),
                                  & wrote)) {
                return 0;
        }

        /*
         * Having written the shim, run it!
         */

        unsigned long   entry = (unsigned long) mem + codeOffset;

        unsigned long   id;
        HANDLE          thread;
        thread = CreateRemoteThread (steam, 0, 0, (LPTHREAD_START_ROUTINE) entry,
                                     0, 0, & id);
        if (thread == 0)
                return 0;

        /*
         * Get the thread's result, which will be either 0 (if the function was
         * not found) or whatever the function returned.
         */

        WaitForSingleObject (thread, INFINITE);

        unsigned long   result;
        GetExitCodeThread (thread, & result);

        /*
         * Read back the content of the shim, so we can get any result data.
         *
         * If the result block is considered a string, that's easiest, although
         * in one of my normal applications (such as if we use this technique
         * for build-acceptance automation and the like) it'll use a general
         * binary serialization format.
         */

        ReadProcessMemory (steam, mem, codeBytes, sizeof (codeBytes), & wrote);

        return result;
}

/**
 * Call into our DLL by injection.
 */

unsigned long callFilter (HANDLE steam, const wchar_t * path,
                          const char * entryPoint, const wchar_t * param = 0) {
        /*
         * Allocate a page of VM inside the target process.
         *
         * The returned address is usable only in the context of the target,
         * but we need it to construct the shim code to load our DLL.
         */

        void          * mem;
        mem = VirtualAllocEx (steam, 0, sizeof (codeBytes), MEM_COMMIT,
                              PAGE_EXECUTE_READWRITE);
        if (mem == 0)
                return 0;

        unsigned long   result;
        int             tries = 1;

        if (strcmp (entryPoint, "SteamFilter") == 0)
                ++ tries;

        for (;;) {
                result = injectFilter (steam, (unsigned char *) mem, path, entryPoint,
                                       param);

                if (result != ~ 0UL || tries < 2)
                        break;

                /*
                 * This is used to indicate that an old instance might have
                 * been left attached, in which case we can try using a special
                 * shim to force it to unload before having another go at
                 * loading our instance.
                 */

                -- tries;
                injectFilter (steam, (unsigned char *) mem, L"steamfilter.dll",
                              0, 0);
        }

        if (strcmp (entryPoint, "FilterUnload") == 0) {
                /*
                 * Give the unload an extra refcount adjustment just in case.
                 */

                injectFilter (steam, (unsigned char *) mem, L"steamfilter.dll",
                              0, 0);
        }

        /*
         * Always unload our shim; if we want to call another function in the
         * loaded DLL, we can just build another shim.
         */

        VirtualFreeEx (steam, mem, 0, MEM_RELEASE);
        return result;
}

/**
 * Call into a filter DLL using a process ID.
 */

bool callFilterId (unsigned long processId, const char * entryPoint,
                   const wchar_t * param) {
        /*
         * Form a full path name to our shim DLL based on our own executable
         * file name.
         */

        wchar_t         path [1024];
        wcscpy_s (path, ARRAY_LENGTH (path), g_appPath);

        wchar_t       * end = wcsrchr (path, '\\');
        if (end == 0)
                return 0;

        ++ end;

        if (wcscpy_s (end, ARRAY_END (path) - end, L"steamfilter.dll") != 0)
                return 0;

        /*
         * Note that PROCESS_QUERY_INFORMATION below is necessary only for
         * Windows 64-bit, as there's a bug in WOW64's emulation of some of the
         * Win32 APIs where they use that access instead (this bug exists in
         * all the 64-bit editions of Windows since Windows Server 2003, and
         * the CreateRemoteThread () documentation is an example of an API that
         * was retrofitted to document this *after* the 64-bit editions of
         * Windows released.
         *
         * [ I'd forgotten about that; about 8 years ago I was contacted by the
         *   Microsoft AppCompat folks when they release Server 2003 64-bit and
         *   our applications were affected by this bug in Windows. ]
         */

        unsigned long   access;
        access = PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                 PROCESS_VM_READ | PROCESS_VM_WRITE |
                 PROCESS_QUERY_INFORMATION;

        HANDLE          proc;
        proc = OpenProcess (access, FALSE, processId);
        if (proc == 0)
                return false;

        /*
         * Inject our shim page and call the desired entry point in the target
         * DLL.
         */

        unsigned long   result;
        result = callFilter (proc, path, entryPoint, param);

        CloseHandle (proc);

        return result == 1;
}

/**@}*/
