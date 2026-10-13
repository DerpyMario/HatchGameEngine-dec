#ifndef ENGINE_UI_SCENEEDITOR_H
#define ENGINE_UI_SCENEEDITOR_H

class TileLayer;

#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>
#include <Engine/Types/Entity.h>
#include <Engine/UI/Selection.h>

class SceneEditor {
private:
    static void GetViewportRect(float* outX, float* outY, float* outW, float* outH);
    static void ScreenToWorld(float screenX, float screenY, float* worldX, float* worldY);
    static void WorldToScreen(float worldX, float worldY, float* screenX, float* screenY);
    static float GetWorldScale();
    static bool IsLayerValid(int index);
    static TileLayer* GetTileLayer(int layerIndex);
    static Uint32* GetTileAddress(int layerIndex, int x, int y);
    static void TileWritten(int layerIndex, int x, int y, Uint32 value);
    static void WriteTile(int layerIndex, int x, int y, Uint32 value);
    static Uint32 MakeBrushTile(Uint32 existing);
    static void PaintAt(int tileX, int tileY);
    static void EraseAt(int tileX, int tileY);
    static void BeginCommand();
    static void EndCommand();
    static void ApplyTileDirect(int layerIndex, int x, int y, Uint32 value);
    static void CaptureStamp();
    static void PlaceStamp(int tileX, int tileY);
    static Entity* PickEntity(float worldX, float worldY);
    static void UpdateViewportInput(float areaX, float areaY, float areaW, float areaH);
    static void DrawViewportOverlay(float areaX, float areaY, float areaW, float areaH);
    static void DrawTilePalette(float width);
    static void DrawToolsPanel();
    static void DrawLayersPanel();
    static void DrawStampsPanel();
    static void DrawEntitiesPanel();

public:
    static bool Active;
    static bool ShowGrid;
    static bool UnsavedChanges;

    static void SetStatus(const char* format, ...);
    static const char* GetStatus();
    static bool HasScene();
    static void Undo();
    static void Redo();
    static void ClearHistory();
    static bool GetScenePath(char* out, size_t outSize);
    static bool CanSave();
    static bool Save();
    static void Reset();
    static void Draw(float x, float y, float width, float height);
};

#endif /* ENGINE_UI_SCENEEDITOR_H */
