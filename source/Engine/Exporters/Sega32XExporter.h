#ifndef ENGINE_EXPORTERS_SEGA32XEXPORTER_H
#define ENGINE_EXPORTERS_SEGA32XEXPORTER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/Sega32XTypes.h>

class Sega32XExporter {
private:
    static Uint32 QuantizeColor(Uint32 argb);
    static long ColorDistance(Uint32 a, Uint32 b);
    static int NearestInPalette(Uint32 color);
    static void GatherColors(TileLayer* layer, int width, int height, vector<X32ColorUse>* out);
    static void BuildImage(TileLayer* layer, int width, int height);
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool CopyFile(const char* from, const char* to);
    static bool FindRuntime(char* out, size_t outSize);
    static bool WriteProject(const char* outputPath, Sega32XExportResult* result);
    static void HeaderField(char* out, size_t width, const char* text);
    static bool WriteSources(const char* outputPath, Sega32XExportResult* result);
    static bool WriteBuildFiles(const char* outputPath, Sega32XExportResult* result);

public:
    static Sega32XExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_SEGA32XEXPORTER_H */
