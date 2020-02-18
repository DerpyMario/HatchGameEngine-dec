#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/HashMap.h>

class ResourceManager {
public:
    static bool      UsingDataFolder;
};
#endif

#include <Engine/ResourceTypes/ResourceManager.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/File.h>
#include <Engine/Hashing/CRC32.h>
#include <Engine/Includes/StandardSDL2.h>
#include <Engine/IO/Compression/ZLibStream.h>
#include <Engine/IO/FileStream.h>
#include <Engine/IO/MemoryStream.h>
#include <Engine/IO/Stream.h>

struct      StreamNode {
    Stream*            Table;
    struct StreamNode* Next;
};
StreamNode* StreamNodeHead = NULL;

struct  ResourceRegistryItem {
    Stream* Table;
    Uint64  Offset;
    Uint64  Size;
    Uint64  CompressedSize;
};
HashMap<ResourceRegistryItem>* ResourceRegistry = NULL;

bool                 ResourceManager::UsingDataFolder = true;

PUBLIC STATIC void   ResourceManager::PrefixResourcePath(char* out, const char* path) {
    #if defined(MACOSX_APP_BUNDLE)
        sprintf(out, "%s", path);
    #elif defined(SWITCH_ROMFS)
        sprintf(out, "romfs:/%s", path);
    #elif defined(ANDROID)
        sprintf(out, "%s", path);
    #else
        sprintf(out, "Resources/%s", path);
    #endif
}
PUBLIC STATIC void   ResourceManager::PrefixParentPath(char* out, const char* path) {
    #if defined(SWITCH_ROMFS)
        sprintf(out, "romfs:/%s", path);
    #else
        sprintf(out, "%s", path);
    #endif
}

PUBLIC STATIC void   ResourceManager::Init(const char* filename) {
    ResourceRegistry = new HashMap<ResourceRegistryItem>(CRC32::EncryptString, 16);

    if (filename == NULL)
        filename = "Data.hatch";

    if (File::Exists(filename)) {
        ResourceManager::UsingDataFolder = false;

        Log::Print(Log::LOG_IMPORTANT, "Using \"%s\"", filename);
        ResourceManager::Load(filename);
    }
    else {
        Log::Print(Log::LOG_INFO, "Cannot find \"%s\".", filename);
    }
}
PUBLIC STATIC void   ResourceManager::Load(const char* filename) {
    if (!ResourceRegistry)
        return;

    // Load directly from Resource folder
    char resourcePath[256];
    ResourceManager::PrefixParentPath(resourcePath, filename);

    SDL_RWops* rw = SDL_RWFromFile(resourcePath, "rb");
    if (!rw) {
        // Log::Print(Log::LOG_ERROR, "ResourceManager::Load: No RW!: %s", resourcePath, SDL_GetError());
        return;
    }

    Sint64 rwSize = SDL_RWsize(rw);
    if (rwSize < 0) {
        Log::Print(Log::LOG_ERROR, "Could not get size of file \"%s\": %s", resourcePath, SDL_GetError());
        return;
    }

    MemoryStream* dataTableStream = MemoryStream::New(rwSize);
    if (!dataTableStream) {
        Log::Print(Log::LOG_ERROR, "Could not open MemoryStream!");
        return;
    }

    SDL_RWread(rw, dataTableStream->pointer_start, rwSize, 1);
    SDL_RWclose(rw);

    Uint16 fileCount;
    Uint8 magicHATCH[5];
    dataTableStream->ReadBytes(magicHATCH, 5);
    if (memcmp(magicHATCH, "HATCH", 5)) {
        Log::Print(Log::LOG_ERROR, "Invalid HATCH data file \"%s\"!", filename);
        dataTableStream->Close();
        return;
    }

    // Uint8 major, minor, pad;
    dataTableStream->ReadByte();
    dataTableStream->ReadByte();
    dataTableStream->ReadByte();

    // Add stream to list for closure on disposal
    StreamNode* streamNode = new StreamNode;
    streamNode->Table = dataTableStream;
    streamNode->Next = StreamNodeHead;
    StreamNodeHead = streamNode;

    Log::Print(Log::LOG_VERBOSE, "Loading resource table from \"%s\"...", filename);
    fileCount = dataTableStream->ReadUInt16();
    for (int i = 0; i < fileCount; i++) {
        Uint32 crc32 = dataTableStream->ReadUInt32();
        Uint64 offset = dataTableStream->ReadUInt64();
        Uint64 size = dataTableStream->ReadUInt64();
        // bool   compressed =
        dataTableStream->ReadUInt32();
        Uint64 compressedSize = dataTableStream->ReadUInt64();

        ResourceRegistryItem item { dataTableStream, offset, size, compressedSize };
        ResourceRegistry->Put(crc32, item);
        // Log::Print(Log::LOG_VERBOSE, "%08X: Offset: %08llX Size: %08llX Comp Size: %08llX", crc32, offset, size, compressedSize);
    }
}
PUBLIC STATIC bool   ResourceManager::LoadResource(const char* filename, Uint8** out, size_t* size) {
    if (ResourceManager::UsingDataFolder) {
        char resourcePath[256];
        ResourceManager::PrefixResourcePath(resourcePath, filename);

        SDL_RWops* rw = SDL_RWFromFile(resourcePath, "rb");
        if (!rw) {
            // Log::Print(Log::LOG_ERROR, "ResourceManager::LoadResource: No RW!: %s", resourcePath, SDL_GetError());
            return false;
        }

        Sint64 rwSize = SDL_RWsize(rw);
        if (rwSize < 0) {
            Log::Print(Log::LOG_ERROR, "Could not get size of file \"%s\": %s", resourcePath, SDL_GetError());
            return false;
        }

        Uint8* memory = (Uint8*)Memory::Malloc(rwSize + 1);
        if (!memory)
            return false;
        memory[rwSize] = 0;

        SDL_RWread(rw, memory, rwSize, 1);
        SDL_RWclose(rw);

        *out = memory;
        *size = rwSize;
        return true;
    }
    if (!ResourceRegistry)
        return false;

    if (!ResourceRegistry->Exists(filename))
        return false;

    ResourceRegistryItem item = ResourceRegistry->Get(filename);

    Uint8* memory = (Uint8*)Memory::Malloc(item.Size + 1);
    if (!memory)
        return false;

    memory[item.Size] = 0;

    item.Table->Seek(item.Offset);
    if (item.Size != item.CompressedSize) {
        Uint8* compressedMemory = (Uint8*)Memory::Malloc(item.Size);
        if (!compressedMemory) {
            Memory::Free(memory);
            return false;
        }
        item.Table->ReadBytes(compressedMemory, item.CompressedSize);

        ZLibStream::Decompress(memory, (size_t)item.Size, compressedMemory, (size_t)item.CompressedSize);
        Memory::Free(compressedMemory);
    }
    else {
        item.Table->ReadBytes(memory, item.Size);
    }

    *out = memory;
    *size = (size_t)item.Size;
    return true;
}
PUBLIC STATIC bool   ResourceManager::ResourceExists(const char* filename) {
    if (ResourceManager::UsingDataFolder) {
        char resourcePath[256];
        ResourceManager::PrefixResourcePath(resourcePath, filename);

        SDL_RWops* rw = SDL_RWFromFile(resourcePath, "rb");
        if (!rw) {
            return false;
        }
        SDL_RWclose(rw);
        return true;
    }

    if (!ResourceRegistry)
        return false;

    if (!ResourceRegistry->Exists(filename))
        return false;

    return true;
}
PUBLIC STATIC void   ResourceManager::Dispose() {
    if (StreamNodeHead) {
        for (StreamNode *old, *streamNode = StreamNodeHead; streamNode; ) {
            old = streamNode;
            streamNode = streamNode->Next;

            old->Table->Close();
            delete old;
        }
    }
    if (ResourceRegistry) {
        delete ResourceRegistry;
    }
}
