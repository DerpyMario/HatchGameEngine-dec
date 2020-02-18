#if INTERFACE

#include <Engine/Includes/Standard.h>

class Memory {
private:
    static vector<void*>       TrackedMemory;
    static vector<size_t>      TrackedSizes;
    static vector<const char*> TrackedMemoryNames;
public:
    static size_t              MemoryUsage;
};
#endif

#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>

#ifdef SWITCH
#define NOTRACK
#endif

vector<void*>        Memory::TrackedMemory;
vector<size_t>       Memory::TrackedSizes;
vector<const char*>  Memory::TrackedMemoryNames;
size_t               Memory::MemoryUsage = 0;

PUBLIC STATIC void   Memory::Memset4(void* dst, Uint32 val, size_t dwords) {
    #if defined(__GNUC__) && defined(i386)
        int u0, u1, u2;
        __asm__ __volatile__ (
            "cld \n\t"
            "rep ; stosl \n\t"
            : "=&D" (u0), "=&a" (u1), "=&c" (u2)
            : "0" (dst), "1" (val), "2" ((Uint32)dwords)
            : "memory"
        );
    #else
        size_t  _n = (dwords + 3) >> 2;
        Uint32* _p = ((Uint32*)dst);
        Uint32  _val = (val);
        if (dwords == 0)
            return;

        switch (dwords & 3) {
            case 0: do {    *_p++ = _val;   /* fallthrough */
            case 3:         *_p++ = _val;   /* fallthrough */
            case 2:         *_p++ = _val;   /* fallthrough */
            case 1:         *_p++ = _val;   /* fallthrough */
            } while (--_n);
        }
    #endif
}

PUBLIC STATIC void*  Memory::Malloc(size_t size) {
    void* mem = malloc(size);
    #ifndef NOTRACK
    if (mem) {
        MemoryUsage += size;

        TrackedMemory.push_back(mem);
        TrackedSizes.push_back(size);
        TrackedMemoryNames.push_back(NULL);
    }
    #endif
    return mem;
}
PUBLIC STATIC void*  Memory::Calloc(size_t count, size_t size) {
    void* mem = calloc(count, size);
    #ifndef NOTRACK
    if (mem) {
        MemoryUsage += count * size;

        TrackedMemory.push_back(mem);
        TrackedSizes.push_back(count * size);
        TrackedMemoryNames.push_back(NULL);
    }
    #endif
    return mem;
}
PUBLIC STATIC void*  Memory::Realloc(void* pointer, size_t size) {
    void* mem = realloc(pointer, size);
    #ifndef NOTRACK
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            MemoryUsage += size - TrackedSizes[i];

            TrackedMemory[i] = mem;
            TrackedSizes[i] = size;
            return mem;
        }
    }
    #endif
    return mem;
}
// Tracking functions
PUBLIC STATIC void*  Memory::TrackedMalloc(const char* identifier, size_t size) {
    void* mem = malloc(size);
    #ifndef NOTRACK
    if (mem) {
        MemoryUsage += size;

        TrackedMemory.push_back(mem);
        TrackedSizes.push_back(size);
        TrackedMemoryNames.push_back(identifier);
    }
    #endif
    return mem;
}
PUBLIC STATIC void*  Memory::TrackedCalloc(const char* identifier, size_t count, size_t size) {
    void* mem = calloc(count, size);
    #ifndef NOTRACK
    if (mem) {
        MemoryUsage += count * size;

        TrackedMemory.push_back(mem);
        TrackedSizes.push_back(count * size);
        TrackedMemoryNames.push_back(identifier);
    }
    #endif
    return mem;
}
PUBLIC STATIC void   Memory::Track(void* pointer, const char* identifier) {
    #ifndef NOTRACK
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            TrackedMemoryNames[i] = identifier;
            return;
        }
    }
    #endif
}
PUBLIC STATIC void   Memory::Track(void* pointer, size_t size, const char* identifier) {
    #ifndef NOTRACK
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            TrackedSizes[i] = size;
            TrackedMemoryNames[i] = identifier;
            return;
        }
    }

    TrackedMemory.push_back(pointer);
    TrackedSizes.push_back(size);
    TrackedMemoryNames.push_back(identifier);
    #endif
}
PUBLIC STATIC void   Memory::TrackLast(const char* identifier) {
    #ifndef NOTRACK
    if (TrackedMemoryNames.size() == 0) return;
    TrackedMemoryNames[TrackedMemoryNames.size() - 1] = identifier;
    #endif
}
PUBLIC STATIC void   Memory::Free(void* pointer) {
    #ifndef NOTRACK
    #ifdef DEBUG
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            // 32-bit
            size_t ptr_size = sizeof(void*);
            if (ptr_size == 4) {
                size_t* debug = (size_t*)TrackedMemory[i];
                for (size_t d = 0, dSz = TrackedSizes[i] / ptr_size; d < dSz; d++) {
                    debug[d] = 0xCDCDCDCDU;
                }
            }
            // 64-bit
            else if (ptr_size == 8) {
                size_t* debug = (size_t*)TrackedMemory[i];
                for (size_t d = 0, dSz = TrackedSizes[i] / ptr_size; d < dSz; d++) {
                    debug[d] = 0xCDCDCDCDCDCDCDCDU;
                }
            }
            break;
        }
    }
    #endif
    #endif
    Memory::Remove(pointer);
    if (!pointer) return;

    free(pointer);
}
PUBLIC STATIC void   Memory::Remove(void* pointer) {
    if (!pointer) return;
    #ifndef NOTRACK
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            MemoryUsage -= TrackedSizes[i];
            // printf("TrackedSizes[i]: %zu\n", TrackedSizes[i]);

            TrackedMemoryNames.erase(TrackedMemoryNames.begin() + i);
            TrackedMemory.erase(TrackedMemory.begin() + i);
            TrackedSizes.erase(TrackedSizes.begin() + i);
            return;
        }
    }
    #endif
}

PUBLIC STATIC const char* Memory::GetName(void* pointer) {
    #ifndef NOTRACK
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        if (TrackedMemory[i] == pointer) {
            return TrackedMemoryNames[i];
        }
    }
    #endif
    return NULL;
}

PUBLIC STATIC void   Memory::ClearTrackedMemory() {
    TrackedMemoryNames.clear();
    TrackedMemory.clear();
    TrackedSizes.clear();
}
PUBLIC STATIC size_t Memory::CheckLeak() {
    size_t total = 0;
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        total += TrackedSizes[i];
    }
    return total;
}
PUBLIC STATIC void   Memory::PrintLeak() {
    size_t total = 0;
    Log::Print(Log::LOG_VERBOSE, "Printing unfreed memory... (%u count)", TrackedMemory.size());
    for (Uint32 i = 0; i < TrackedMemory.size(); i++) {
        Log::Print(Log::LOG_VERBOSE, " : %p [%u bytes] (%s)", TrackedMemory[i], TrackedSizes[i], TrackedMemoryNames[i] ? TrackedMemoryNames[i] : "no name");
        total += TrackedSizes[i];
    }
    Log::Print(Log::LOG_VERBOSE, "Total: %u bytes (%.3f MB)", total, total / 1024 / 1024.0);
}
