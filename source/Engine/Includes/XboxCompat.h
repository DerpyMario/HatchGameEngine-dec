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

// nxdk builds everything without C++ exceptions: its C++ library has no
// unwinder to give, and the compiler will not accept try, catch or throw at
// all. The engine reports script errors by throwing, so those three are given
// meanings it can compile here.
//
// A try block always runs, and its handlers never do -- nothing can be caught
// that was never thrown. A throw cannot unwind, so it says what went wrong and
// stops the title, which is what an uncaught exception would have done; the
// difference is that a script error the engine would have recovered from
// elsewhere stops the game on the Xbox.
#ifdef __cplusplus

// nxdk's C++ library still spells some exception specifications throw(), so
// every standard header that does is read before throw means anything else.
// Their include guards keep them from being read again afterwards.
// clang-format off
#include <algorithm>
#include <codecvt>
#include <deque>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <ostream>
#include <regex>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
// clang-format on

[[noreturn]] void XboxThrowFailed(const char* message);

struct XboxThrow {};

template<typename T>
[[noreturn]] inline void operator<<(XboxThrow, const T& exception) {
    XboxThrowFailed(exception.what());
}

// Only ever named in a handler that cannot run, so it is never called; it
// exists to give a handler's declaration something to be initialised from.
struct XboxCaught {
    template<typename T>
    operator T&() const {
        __builtin_unreachable();
    }
};

#define try if (true)
#define catch(declaration) else if (false) for (declaration = XboxCaught(); false;)
#define throw XboxThrow() <<

#endif /* __cplusplus */

// stb_image's SSE2 paths: the Xbox's Pentium III has SSE, but not SSE2.
#define STBI_NO_SIMD

#endif /* XBOXCOMPAT_H */
