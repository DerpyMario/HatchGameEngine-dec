#ifndef ENGINE_EXPORTERS_MEGACDEXPORTER_H
#define ENGINE_EXPORTERS_MEGACDEXPORTER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/MegaCDTypes.h>

class MegaCDExporter {
private:
    static int PlaneSizeCode(int cells);
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool WriteProject(const char* outputPath, MegaCDExportResult* result);
    static bool WritePlayer(const char* outputPath, MegaCDExportResult* result);

public:
    static MegaCDExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_MEGACDEXPORTER_H */
