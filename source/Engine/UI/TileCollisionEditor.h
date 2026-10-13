#ifndef ENGINE_UI_TILECOLLISIONEDITOR_H
#define ENGINE_UI_TILECOLLISIONEDITOR_H

#include <Engine/Includes/Standard.h>
#include <Engine/UI/TileCollisionTypes.h>

class TileCollisionEditor {
private:
    static void SetStatus(const char* format, ...);
    static void ReadEntryFromScene(int plane, int tileID, TileCollisionEntry* entry);
    static void ApplyToScene(int plane, int tileID);
    static void RefreshHasCollision(TileCollisionEntry* entry);
    static void PaintColumn(TileCollisionEntry* entry, int column, int row);
    static void DrawTileCanvas(float width);
    static void DrawTilePicker(float width);

public:
    static bool UnsavedChanges;

    static const char* GetStatus();
    static bool IsAvailable();
    static void Reload();
    static void Invalidate();
    static bool GetCollisionPath(char* out, size_t outSize);
    static bool CanSave();
    static bool Save();
    static void Draw(float x, float y, float width, float height, bool split);
};

#endif /* ENGINE_UI_TILECOLLISIONEDITOR_H */
