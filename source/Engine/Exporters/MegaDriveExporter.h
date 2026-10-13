#ifndef ENGINE_EXPORTERS_MEGADRIVEEXPORTER_H
#define ENGINE_EXPORTERS_MEGADRIVEEXPORTER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/MegaDriveTypes.h>

class MegaDriveExporter {
private:
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool WriteProject(const char* outputPath, MegaDriveExportResult* result);
    static bool WritePlayer(const char* outputPath, MegaDriveExportResult* result);

public:
    static MegaDriveExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_MEGADRIVEEXPORTER_H */
