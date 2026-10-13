#ifdef XBOX

#include <Engine/Diagnostics/Log.h>
#include <Engine/Error.h>

// What a throw does on the Xbox, which has nothing to unwind with; see
// XboxCompat.h.
void XboxThrowFailed(const char* message) {
    Error::Fatal("%s", message ? message : "An error the engine could not recover from.");

    // Error::Fatal does not return, but it is not declared as such.
    for (;;) {
    }
}

#endif /* XBOX */
