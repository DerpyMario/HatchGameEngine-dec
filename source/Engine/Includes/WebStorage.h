#ifndef WEBSTORAGE_H
#define WEBSTORAGE_H

#ifdef EMSCRIPTEN

#include <emscripten.h>
#include <string.h>

// A page writes to a filesystem that lives exactly as long as the tab does.
// One directory is different: meta/web/shell.html mounts this path from
// IndexedDB before main runs, so what is under it survives a reload. What is
// written there still has to be pushed back, which is what Flush does.
//
// Both stream types call these, since settings go through one and saved games
// through the other.

#define WEB_SAVE_PATH "/save"

static inline bool WebStorage_IsPersistent(const char* path) {
    if (!path)
        return false;

    return strncmp(path, WEB_SAVE_PATH "/", sizeof(WEB_SAVE_PATH "/") - 1) == 0;
}

// Asynchronous, and deliberately not waited on: the write has already landed in
// the filesystem the engine reads back, and holding a frame for IndexedDB to
// answer would be worse than the small window where a tab closed in the next
// instant loses the last save.
static inline void WebStorage_Flush() {
    EM_ASM({
        FS.syncfs(false, function (err) {
            if (err)
                console.error('could not write saves back to storage:', err);
        });
    });
}

#endif /* EMSCRIPTEN */

#endif /* WEBSTORAGE_H */
