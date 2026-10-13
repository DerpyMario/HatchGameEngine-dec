#ifndef ENGINE_EXPORTERS_GAMEGEAREXPORTER_H
#define ENGINE_EXPORTERS_GAMEGEAREXPORTER_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/GameGearTypes.h>

class GameGearExporter {
private:
    static Uint32 QuantizeColor(Uint32 native);
    static long ColorDistance(Uint32 a, Uint32 b);
    static void BuildCells(TileLayer* layer, int mapW, int mapH);
    static void CellColors(GameGearCell* cell, vector<Uint32>* out);
    static int MissingFrom(GameGearPalette* palette, vector<Uint32>* colors);
    static void AssignPalettes();
    static void PackCell(GameGearCell* cell, GameGearTile* out);
    static void FlipTile(GameGearTile* in, bool flipX, bool flipY, GameGearTile* out);
    static int AddPattern(GameGearTile* tile, bool* flipX, bool* flipY);
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool WriteProject(const char* outputPath, GameGearExportResult* result);
    static bool WriteDataArrays(const char* outputPath, GameGearExportResult* result);
    static bool WriteSources(const char* outputPath, GameGearExportResult* result);

public:
    static GameGearExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_GAMEGEAREXPORTER_H */
