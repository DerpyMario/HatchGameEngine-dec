#ifndef XBOXCOMPAT_H
#define XBOXCOMPAT_H

// Force-included into every translation unit of the Xbox build.
//
// nxdk compiles with clang targeting i386-pc-win32, which defines _MSC_VER, so
// the libraries the engine vendors take their Microsoft compiler paths and
// reach for the optional bounds-checked functions of C11's Annex K. nxdk's C
// library does not have those, and the alternative -- convincing every one of
// those libraries it is not on Microsoft's compiler -- would mean fighting the
// same toolchain that supplies the Windows API they otherwise want.
//
// So the few that are actually used are supplied here instead.

#include <stdio.h>

#ifndef fopen_s
static inline int fopen_s(FILE** streamptr, const char* filename, const char* mode) {
    if (!streamptr)
        return 1;

    *streamptr = fopen(filename, mode);

    return *streamptr ? 0 : 1;
}
#endif

// pdclib does not carry these three. atof is C89 but optional in a freestanding
// library; the other two are the Microsoft spellings of working-directory
// calls, which the filesystem code reaches for on the Windows side it shares
// with the Xbox.

#include <stdlib.h>
#include <string.h>

#ifndef atof
static inline double atof(const char* s) {
    return strtod(s, NULL);
}
#endif

// A title runs from the drive it was launched off, which nxdk mounts as D:.
// There is no working directory to move around in the way there is on a PC.
static inline char* _getcwd(char* buffer, unsigned int size) {
    if (!buffer || size < 4)
        return NULL;

    strncpy(buffer, "D:\\", size);
    buffer[size - 1] = '\0';

    return buffer;
}

static inline int _chdir(const char* path) {
    (void)path;
    return 0;
}

#endif /* XBOXCOMPAT_H */
