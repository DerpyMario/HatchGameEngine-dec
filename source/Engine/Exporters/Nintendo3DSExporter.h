#ifndef ENGINE_EXPORTERS_NINTENDO3DSEXPORTER_H
#define ENGINE_EXPORTERS_NINTENDO3DSEXPORTER_H

#include <Engine/Includes/Standard.h>

struct Nintendo3DSExportResult {
    bool   Success;

    int    FileCount;     // files packed into Data.hatch
    size_t DataBytes;     // what they add up to before compression
    size_t PackedBytes;   // Data.hatch itself
    size_t RuntimeBytes;  // the engine's code, as taken from the runtime
    size_t OutputBytes;   // the .3dsx that was written

    char   Message[512];
};

// A game for the 3DS, as one .3dsx the Homebrew Launcher can run.
//
// Unlike the SEGA machines, the 3DS runs the engine itself, so nothing about
// the game is converted: its Resources folder is packed into a Data.hatch, the
// way a game is shipped anywhere else, and that is put in the RomFS of a copy
// of the engine built for the 3DS -- the runtime. The title, description and
// developer from GameConfig go in its SMDH, which is what the Homebrew
// Launcher and the HOME Menu show.
class Nintendo3DSExporter {
private:
    static bool ReadFile(const char* path, vector<Uint8>* out);
    static bool WriteFile(const char* path, const vector<Uint8>& data);
    static bool FindRuntime(const char* requested, std::string* out);
    static bool PackFolder(const char* folder, vector<Uint8>* out, Nintendo3DSExportResult* result);
    static void BuildRomFS(const char* name, const vector<Uint8>& data, vector<Uint8>* out);
    static void PutTitle(Uint8* field, size_t fieldBytes, const char* text);
    static bool LoadIcon(const char* path, vector<Uint32>* rgba, int* width, int* height);
    static void PutIcon(Uint8* out, int size, const vector<Uint32>& rgba, int width, int height);
    static bool BuildSMDH(const Uint8* runtimeSMDH, const char* iconPath, vector<Uint8>* out, char* error, size_t errorSize);

public:
    static Nintendo3DSExportResult Export(const char* outputPath, const char* runtimePath, const char* iconPath);
};

#endif /* ENGINE_EXPORTERS_NINTENDO3DSEXPORTER_H */
