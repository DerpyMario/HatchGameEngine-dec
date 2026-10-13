#include <Engine/Exporters/Nintendo3DSExporter.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Filesystem/File.h>
#include <Engine/Filesystem/Path.h>
#include <Engine/Graphics.h>
#include <Engine/Hashing/CRC32.h>
#include <Engine/IO/Compression/ZLibStream.h>
#include <Engine/ResourceTypes/ImageFormats/PNG.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/Utilities/ColorUtils.h>
#include <Engine/Utilities/StringUtils.h>

// Packing a game into a .3dsx.
//
// A .3dsx is the engine's code with, optionally, two things appended: an SMDH,
// which is the title and icon the Homebrew Launcher shows, and a RomFS, which
// is a read-only filesystem the program mounts as romfs:/. The header says
// where each of them starts. The runtime is a .3dsx devkitPro built from the
// engine; this keeps its code, gives it a new SMDH, and puts the game in its
// RomFS, where the engine looks for a Data.hatch when it starts.

#define DATA_FILE_NAME "Data.hatch"

#define _3DSX_HEADER_SIZE 0x20
#define _3DSX_EXTENDED_HEADER_SIZE 0x2C

#define SMDH_SIZE 0x36C0
#define SMDH_TITLES_OFFSET 0x8
#define SMDH_TITLE_SIZE 0x200
#define SMDH_TITLE_COUNT 16
#define SMDH_SETTINGS_OFFSET 0x2008
#define SMDH_SMALL_ICON_OFFSET 0x2040
#define SMDH_LARGE_ICON_OFFSET 0x24C0

#define ROMFS_HEADER_SIZE 0x28
#define ROMFS_NONE 0xFFFFFFFFU

static Uint32 ReadU32(const Uint8* data) {
    return (Uint32)data[0] | ((Uint32)data[1] << 8) | ((Uint32)data[2] << 16) | ((Uint32)data[3] << 24);
}

static Uint16 ReadU16(const Uint8* data) {
    return (Uint16)(data[0] | (data[1] << 8));
}

static void PutU16(Uint8* out, Uint16 value) {
    out[0] = value & 0xFF;
    out[1] = (value >> 8) & 0xFF;
}

static void PutU32(Uint8* out, Uint32 value) {
    out[0] = value & 0xFF;
    out[1] = (value >> 8) & 0xFF;
    out[2] = (value >> 16) & 0xFF;
    out[3] = (value >> 24) & 0xFF;
}

static void PushU16(vector<Uint8>* out, Uint16 value) {
    Uint8 bytes[2];
    PutU16(bytes, value);
    out->insert(out->end(), bytes, bytes + 2);
}

static void PushU32(vector<Uint8>* out, Uint32 value) {
    Uint8 bytes[4];
    PutU32(bytes, value);
    out->insert(out->end(), bytes, bytes + 4);
}

static void PushU64(vector<Uint8>* out, Uint64 value) {
    PushU32(out, (Uint32)(value & 0xFFFFFFFFU));
    PushU32(out, (Uint32)(value >> 32));
}

static void PadTo(vector<Uint8>* out, size_t alignment) {
    while (out->size() % alignment)
        out->push_back(0);
}

// UTF-8 to UTF-16, which is what the 3DS stores text as. Anything past the
// Basic Multilingual Plane becomes a surrogate pair; anything that does not
// decode becomes a question mark rather than ending the string.
static vector<Uint16> ToUTF16(const char* text) {
    vector<Uint16> out;

    while (text && *text) {
        int length = 1;
        int codepoint = StringUtils::DecodeUTF8Char(text, length);
        if (codepoint < 0)
            codepoint = '?';

        if (codepoint >= 0x10000) {
            codepoint -= 0x10000;
            out.push_back((Uint16)(0xD800 + (codepoint >> 10)));
            out.push_back((Uint16)(0xDC00 + (codepoint & 0x3FF)));
        }
        else {
            out.push_back((Uint16)codepoint);
        }

        text += length;
    }

    return out;
}

bool Nintendo3DSExporter::ReadFile(const char* path, vector<Uint8>* out) {
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return false;
    }

    out->resize((size_t)size);
    bool ok = size == 0 || fread(out->data(), 1, (size_t)size, f) == (size_t)size;
    fclose(f);

    return ok;
}

bool Nintendo3DSExporter::WriteFile(const char* path, const vector<Uint8>& data) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = data.size() == 0 || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);

    return ok;
}

// The runtime the export was pointed at, or failing that one sitting beside the
// engine doing the exporting, which is where a release would put it.
bool Nintendo3DSExporter::FindRuntime(const char* requested, std::string* out) {
    if (requested && requested[0]) {
        *out = requested;
        return File::Exists(requested);
    }

    char* base = SDL_GetBasePath();
    if (base) {
        *out = std::string(base) + "HatchGameEngine.3dsx";
        SDL_free(base);

        if (File::Exists(out->c_str()))
            return true;
    }

    return false;
}

// Every file under the folder, as a .hatch: the same archive the engine reads a
// shipped game from, and so the same one it reads this one from on the 3DS. An
// entry is named by the CRC-32 of its path inside the folder, which is what the
// engine looks it up by; it is kept compressed when that makes it smaller.
bool Nintendo3DSExporter::PackFolder(const char* folder, vector<Uint8>* out, Nintendo3DSExportResult* result) {
    std::vector<std::filesystem::path> found;
    Directory::GetFiles(&found, folder, "*", true);

    std::string root = Path::Normalize(Path::ToAbsolute(std::string(folder)));
    if (root.size() && root.back() != '/')
        root += '/';

    struct Entry {
        Uint32 Hash;
        Uint64 Size;
        vector<Uint8> Stored;
    };
    vector<Entry> entries;

    for (size_t i = 0; i < found.size(); i++) {
        std::string path = Path::Normalize(Path::ToAbsolute(Path::ToString(found[i])));
        if (!StringUtils::StartsWith(path.c_str(), root.c_str()))
            continue;

        std::string name = path.substr(root.size());

        // Hidden files are the operating system's or a tool's, not the game's.
        std::string filename = StringUtils::GetFilename(name.c_str());
        if (filename.size() == 0 || filename[0] == '.')
            continue;

        vector<Uint8> data;
        if (!ReadFile(path.c_str(), &data)) {
            snprintf(result->Message, sizeof(result->Message), "Could not read \"%s\".", path.c_str());
            return false;
        }

        Entry entry;
        entry.Hash = CRC32::EncryptString(name.c_str());
        entry.Size = data.size();

        void* compressed = nullptr;
        size_t compressedSize = 0;
        if (data.size() && ZLibStream::Compress(data.data(), data.size(), &compressed, &compressedSize) &&
            compressedSize < data.size()) {
            entry.Stored.assign((Uint8*)compressed, (Uint8*)compressed + compressedSize);
        }
        else {
            // The reader takes an entry whose two sizes match to be stored
            // as it is, so one that would not shrink is kept that way.
            entry.Stored = data;
        }
        if (compressed)
            Memory::Free(compressed);

        for (size_t e = 0; e < entries.size(); e++) {
            if (entries[e].Hash == entry.Hash) {
                snprintf(result->Message, sizeof(result->Message),
                    "\"%s\" has the same name hash as another file, so the engine could not tell them apart.",
                    name.c_str());
                return false;
            }
        }

        result->DataBytes += data.size();
        entries.push_back(entry);
    }

    if (entries.size() == 0) {
        snprintf(result->Message, sizeof(result->Message), "There are no files in \"%s\" to pack.", folder);
        return false;
    }
    if (entries.size() > 0xFFFF) {
        snprintf(result->Message, sizeof(result->Message),
            "%d files is more than a .hatch can list (65535).", (int)entries.size());
        return false;
    }

    // Magic, version 1.0.0, the file count, then a 32-byte table entry for
    // each file, then the files.
    const char magic[] = { 'H', 'A', 'T', 'C', 'H' };
    out->insert(out->end(), magic, magic + sizeof(magic));
    out->push_back(1);
    out->push_back(0);
    out->push_back(0);
    PushU16(out, (Uint16)entries.size());

    Uint64 offset = out->size() + 32 * entries.size();
    for (size_t e = 0; e < entries.size(); e++) {
        PushU32(out, entries[e].Hash);
        PushU64(out, offset);
        PushU64(out, entries[e].Size);
        PushU32(out, 0); // not encrypted
        PushU64(out, entries[e].Stored.size());

        offset += entries[e].Stored.size();
    }

    for (size_t e = 0; e < entries.size(); e++)
        out->insert(out->end(), entries[e].Stored.begin(), entries[e].Stored.end());

    result->FileCount = (int)entries.size();

    return true;
}

// The hash libctru uses to find a name in a RomFS directory: the parent's
// offset, folded with each UTF-16 unit of the name.
static Uint32 RomFSHash(Uint32 parent, const vector<Uint16>& name, Uint32 buckets) {
    Uint32 hash = parent ^ 123456789U;
    for (size_t i = 0; i < name.size(); i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= name[i];
    }
    return hash % buckets;
}

// A RomFS holding one file at its root. This is the filesystem's third level
// on its own, which is what a .3dsx carries: the hash trees around it on a
// cartridge are there to be checked against a signature, and a .3dsx has none.
//
//   header
//   directory hash table, directory entries   (just the root)
//   file hash table, file entries             (just the one)
//   file data
//
// Offsets in the entries count from the start of their own table, and the
// data's from the start of the data.
void Nintendo3DSExporter::BuildRomFS(const char* name, const vector<Uint8>& data, vector<Uint8>* out) {
    vector<Uint16> fileName = ToUTF16(name);
    vector<Uint16> rootName;

    const Uint32 dirBuckets = 3;
    const Uint32 fileBuckets = 3;

    // The root, at offset 0 of the directory table.
    vector<Uint8> dirTable;
    PushU32(&dirTable, 0);          // parent: the root is its own
    PushU32(&dirTable, ROMFS_NONE); // next sibling
    PushU32(&dirTable, ROMFS_NONE); // first child directory
    PushU32(&dirTable, 0);          // first file: the one at offset 0
    PushU32(&dirTable, ROMFS_NONE); // next in its hash bucket
    PushU32(&dirTable, 0);          // name length, in bytes

    vector<Uint32> dirHash(dirBuckets, ROMFS_NONE);
    dirHash[RomFSHash(0, rootName, dirBuckets)] = 0;

    // The file, at offset 0 of the file table, its data at offset 0 of the
    // data.
    vector<Uint8> fileTable;
    PushU32(&fileTable, 0);          // parent: the root
    PushU32(&fileTable, ROMFS_NONE); // next sibling
    PushU64(&fileTable, 0);          // data offset
    PushU64(&fileTable, data.size());
    PushU32(&fileTable, ROMFS_NONE); // next in its hash bucket
    PushU32(&fileTable, (Uint32)(fileName.size() * 2));
    for (size_t i = 0; i < fileName.size(); i++)
        PushU16(&fileTable, fileName[i]);
    PadTo(&fileTable, 4);

    vector<Uint32> fileHash(fileBuckets, ROMFS_NONE);
    fileHash[RomFSHash(0, fileName, fileBuckets)] = 0;

    Uint32 dirHashOffset = ROMFS_HEADER_SIZE;
    Uint32 dirTableOffset = dirHashOffset + dirBuckets * 4;
    Uint32 fileHashOffset = dirTableOffset + (Uint32)dirTable.size();
    Uint32 fileTableOffset = fileHashOffset + fileBuckets * 4;
    Uint32 dataOffset = fileTableOffset + (Uint32)fileTable.size();
    dataOffset = (dataOffset + 0xF) & ~0xFU;

    out->clear();
    PushU32(out, ROMFS_HEADER_SIZE);
    PushU32(out, dirHashOffset);
    PushU32(out, dirBuckets * 4);
    PushU32(out, dirTableOffset);
    PushU32(out, (Uint32)dirTable.size());
    PushU32(out, fileHashOffset);
    PushU32(out, fileBuckets * 4);
    PushU32(out, fileTableOffset);
    PushU32(out, (Uint32)fileTable.size());
    PushU32(out, dataOffset);

    for (size_t i = 0; i < dirHash.size(); i++)
        PushU32(out, dirHash[i]);
    out->insert(out->end(), dirTable.begin(), dirTable.end());
    for (size_t i = 0; i < fileHash.size(); i++)
        PushU32(out, fileHash[i]);
    out->insert(out->end(), fileTable.begin(), fileTable.end());
    PadTo(out, 0x10);

    out->insert(out->end(), data.begin(), data.end());
}

// A title field holds UTF-16 and has to keep a terminator, so anything longer
// is cut short.
void Nintendo3DSExporter::PutTitle(Uint8* field, size_t fieldBytes, const char* text) {
    memset(field, 0, fieldBytes);

    vector<Uint16> units = ToUTF16(text);
    size_t room = fieldBytes / 2 - 1;
    if (units.size() > room)
        units.resize(room);

    for (size_t i = 0; i < units.size(); i++)
        PutU16(field + i * 2, units[i]);
}

bool Nintendo3DSExporter::LoadIcon(const char* path, vector<Uint32>* rgba, int* width, int* height) {
    Stream* stream = File::Open(path, File::READ_ACCESS);
    if (!stream)
        return false;

    // Palettes would hand the pixels back as indices.
    bool usePalettes = Graphics::UsePalettes;
    Graphics::UsePalettes = false;
    PNG* png = PNG::Load(stream);
    Graphics::UsePalettes = usePalettes;
    stream->Close();

    if (!png)
        return false;

    *width = (int)png->Width;
    *height = (int)png->Height;
    rgba->resize(png->Width * png->Height);

    Uint32* pixels = (Uint32*)png->Data;
    for (size_t i = 0; i < rgba->size(); i++) {
        Uint8 r, g, b, a;
        ColorUtils::GetChannels(pixels[i], Graphics::PreferredPixelFormat, r, g, b, a);
        (*rgba)[i] = ((Uint32)r << 24) | ((Uint32)g << 16) | ((Uint32)b << 8) | a;
    }

    delete png;

    return *width > 0 && *height > 0;
}

// An icon is RGB565, in 8x8 tiles, the pixels of each tile in Z order -- the
// order the 3DS's GPU reads textures in. The picture is averaged down (or
// repeated up) to the size, and anything see-through is laid over white, which
// is what the HOME Menu shows behind an icon.
void Nintendo3DSExporter::PutIcon(Uint8* out, int size, const vector<Uint32>& rgba, int width, int height) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int x0 = x * width / size, x1 = (x + 1) * width / size;
            int y0 = y * height / size, y1 = (y + 1) * height / size;
            if (x1 <= x0)
                x1 = x0 + 1;
            if (y1 <= y0)
                y1 = y0 + 1;

            Uint32 r = 0, g = 0, b = 0, count = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    Uint32 pixel = rgba[sy * width + sx];
                    Uint32 a = pixel & 0xFF;
                    r += (((pixel >> 24) & 0xFF) * a + 255 * (255 - a)) / 255;
                    g += (((pixel >> 16) & 0xFF) * a + 255 * (255 - a)) / 255;
                    b += (((pixel >> 8) & 0xFF) * a + 255 * (255 - a)) / 255;
                    count++;
                }
            }
            r /= count;
            g /= count;
            b /= count;

            Uint16 color = (Uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));

            int tile = (y / 8) * (size / 8) + (x / 8);
            int tx = x & 7, ty = y & 7;
            int inTile = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);

            PutU16(out + (tile * 64 + inTile) * 2, color);
        }
    }
}

// The runtime's SMDH, if it has one, with the game's titles in place of the
// engine's. Its icon stays unless the export was given one of its own, and so
// do its settings, which are devkitPro's defaults: visible, every region.
bool Nintendo3DSExporter::BuildSMDH(const Uint8* runtimeSMDH, const char* iconPath, vector<Uint8>* out, char* error, size_t errorSize) {
    out->assign(SMDH_SIZE, 0);
    Uint8* smdh = out->data();

    if (runtimeSMDH) {
        memcpy(smdh, runtimeSMDH, SMDH_SIZE);
    }
    else {
        memcpy(smdh, "SMDH", 4);

        // Age ratings left unrated, every region allowed, and visible.
        static const Uint8 ratings[16] = { 0xA0, 0xA0, 0x00, 0xA0, 0xA0, 0x00, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0 };
        memcpy(smdh + SMDH_SETTINGS_OFFSET, ratings, sizeof(ratings));
        PutU32(smdh + SMDH_SETTINGS_OFFSET + 0x10, 0xFFFFFFFFU);
        PutU32(smdh + SMDH_SETTINGS_OFFSET + 0x20, 0x141);
    }

    // A game that never set a description has the engine's placeholder,
    // which is not something to put under its name.
    const char* shortTitle = Application::GameTitleShort[0] ? Application::GameTitleShort : Application::GameTitle;
    const char* longTitle = Application::GameTitle;
    if (Application::GameDescription[0] && strcmp(Application::GameDescription, DEFAULT_GAME_DESCRIPTION) != 0)
        longTitle = Application::GameDescription;
    const char* publisher = Application::GameDeveloper[0] ? Application::GameDeveloper : "Unknown";

    // The same text in every language: the 3DS shows whichever matches the
    // system's, and the game only has the one.
    for (int i = 0; i < SMDH_TITLE_COUNT; i++) {
        Uint8* title = smdh + SMDH_TITLES_OFFSET + i * SMDH_TITLE_SIZE;
        PutTitle(title, 0x80, shortTitle);
        PutTitle(title + 0x80, 0x100, longTitle);
        PutTitle(title + 0x180, 0x80, publisher);
    }

    if (iconPath && iconPath[0]) {
        vector<Uint32> rgba;
        int width = 0, height = 0;
        if (!LoadIcon(iconPath, &rgba, &width, &height)) {
            snprintf(error, errorSize, "Could not read the icon \"%s\"; it has to be a PNG.", iconPath);
            return false;
        }

        PutIcon(smdh + SMDH_SMALL_ICON_OFFSET, 24, rgba, width, height);
        PutIcon(smdh + SMDH_LARGE_ICON_OFFSET, 48, rgba, width, height);
    }

    return true;
}

Nintendo3DSExportResult Nintendo3DSExporter::Export(const char* outputPath, const char* runtimePath, const char* iconPath) {
    Nintendo3DSExportResult result;
    memset(&result, 0, sizeof(result));

    // ---- the runtime ----

    std::string runtimeFile;
    if (!FindRuntime(runtimePath, &runtimeFile)) {
        snprintf(result.Message, sizeof(result.Message),
            "No 3DS runtime at \"%s\". Build the engine for the 3DS (see the README) and pass its .3dsx with --3ds-runtime.",
            runtimeFile.size() ? runtimeFile.c_str() : "HatchGameEngine.3dsx");
        return result;
    }

    vector<Uint8> runtime;
    if (!ReadFile(runtimeFile.c_str(), &runtime) || runtime.size() < _3DSX_HEADER_SIZE ||
        memcmp(runtime.data(), "3DSX", 4) != 0) {
        snprintf(result.Message, sizeof(result.Message), "\"%s\" is not a .3dsx.", runtimeFile.c_str());
        return result;
    }

    // Where the program ends: the header, the three relocation headers, the
    // code, rodata and data -- less the part of the data that is zeroes and
    // so not stored -- and the relocations themselves. Anything after that is
    // an SMDH or a RomFS, which are what get replaced.
    const Uint8* header = runtime.data();
    Uint16 headerSize = ReadU16(header + 4);
    Uint16 relocHeaderSize = ReadU16(header + 6);
    Uint32 codeSize = ReadU32(header + 0x10);
    Uint32 rodataSize = ReadU32(header + 0x14);
    Uint32 dataSize = ReadU32(header + 0x18);
    Uint32 bssSize = ReadU32(header + 0x1C);

    size_t programEnd = (size_t)headerSize + 3 * (size_t)relocHeaderSize;
    if (headerSize < _3DSX_HEADER_SIZE || relocHeaderSize < 8 || bssSize > dataSize || programEnd > runtime.size()) {
        snprintf(result.Message, sizeof(result.Message), "\"%s\" has a header this does not understand.", runtimeFile.c_str());
        return result;
    }

    size_t relocations = 0;
    for (int i = 0; i < 3; i++) {
        const Uint8* reloc = header + headerSize + i * relocHeaderSize;
        relocations += ((size_t)ReadU32(reloc) + ReadU32(reloc + 4)) * 4;
    }
    programEnd += (size_t)codeSize + rodataSize + (dataSize - bssSize) + relocations;

    if (programEnd > runtime.size()) {
        snprintf(result.Message, sizeof(result.Message), "\"%s\" is shorter than its header says.", runtimeFile.c_str());
        return result;
    }

    const Uint8* runtimeSMDH = nullptr;
    if (headerSize >= _3DSX_EXTENDED_HEADER_SIZE) {
        Uint32 smdhOffset = ReadU32(header + 0x20);
        Uint32 smdhSize = ReadU32(header + 0x24);
        if (smdhSize == SMDH_SIZE && (size_t)smdhOffset + smdhSize <= runtime.size() &&
            memcmp(header + smdhOffset, "SMDH", 4) == 0)
            runtimeSMDH = header + smdhOffset;
    }

    result.RuntimeBytes = programEnd;

    // ---- the game ----

    vector<Uint8> dataHatch;
    if (ResourceManager::DataFolderPath[0]) {
        if (!PackFolder(ResourceManager::DataFolderPath, &dataHatch, &result))
            return result;
    }
    else if (ResourceManager::DataFilePath[0]) {
        // Already a .hatch, so it goes in as it is.
        if (!ReadFile(ResourceManager::DataFilePath, &dataHatch)) {
            snprintf(result.Message, sizeof(result.Message), "Could not read \"%s\".", ResourceManager::DataFilePath);
            return result;
        }
        result.DataBytes = dataHatch.size();
    }
    else {
        StringUtils::Copy(result.Message,
            "There is no game loaded to export. Point the engine at one with --project-dir or --resource-file.",
            sizeof(result.Message));
        return result;
    }
    result.PackedBytes = dataHatch.size();

    vector<Uint8> romfs;
    BuildRomFS(DATA_FILE_NAME, dataHatch, &romfs);

    vector<Uint8> smdh;
    if (!BuildSMDH(runtimeSMDH, iconPath, &smdh, result.Message, sizeof(result.Message)))
        return result;

    // ---- the .3dsx ----

    // The extended header has room for where the SMDH and the RomFS are.
    // A runtime without one gets it, and everything after moves along.
    vector<Uint8> output;
    output.insert(output.end(), runtime.begin(), runtime.begin() + _3DSX_HEADER_SIZE);
    output.resize(_3DSX_EXTENDED_HEADER_SIZE, 0);
    output.insert(output.end(), runtime.begin() + headerSize, runtime.begin() + programEnd);

    Uint32 smdhOffset = (Uint32)output.size();
    Uint32 romfsOffset = smdhOffset + SMDH_SIZE;

    PutU16(&output[4], _3DSX_EXTENDED_HEADER_SIZE);
    PutU32(&output[0x20], smdhOffset);
    PutU32(&output[0x24], SMDH_SIZE);
    PutU32(&output[0x28], romfsOffset);

    output.insert(output.end(), smdh.begin(), smdh.end());
    output.insert(output.end(), romfs.begin(), romfs.end());

    if (!WriteFile(outputPath, output)) {
        snprintf(result.Message, sizeof(result.Message), "Could not write \"%s\".", outputPath);
        return result;
    }

    result.OutputBytes = output.size();
    result.Success = true;

    if (result.FileCount) {
        snprintf(result.Message, sizeof(result.Message),
            "Wrote %s: %d file(s), %zu bytes packed into %zu, on %zu bytes of engine; %zu bytes in all.",
            outputPath,
            result.FileCount,
            result.DataBytes,
            result.PackedBytes,
            result.RuntimeBytes,
            result.OutputBytes);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Wrote %s: the game's %zu-byte .hatch as it was, on %zu bytes of engine; %zu bytes in all.",
            outputPath,
            result.PackedBytes,
            result.RuntimeBytes,
            result.OutputBytes);
    }

    return result;
}
