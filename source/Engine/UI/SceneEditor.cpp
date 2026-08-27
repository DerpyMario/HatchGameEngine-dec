#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>
#include <Engine/Types/Entity.h>
#include <Engine/UI/Selection.h>

class SceneEditor {
public:
    static bool Active;
    static bool ShowGrid;
    static bool UnsavedChanges;
};
#endif

#include <Engine/UI/SceneEditor.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Application.h>
#include <Engine/Graphics.h>
#include <Engine/Scene.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Scene/SceneEnums.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/ResourceTypes/SceneFormats/TiledMapWriter.h>
#include <Engine/Types/ObjectList.h>
#include <Engine/Utilities/StringUtils.h>

// The scene editor, following what HatchStudio's SceneEditor offers: a tile
// palette and brush, rectangle selection, stamps, collision editing, layer
// controls, entity picking, and an undo stack behind all of it.
//
// HatchStudio is a separate program, so it keeps its own copy of the scene and
// reads the file into it. Here the scene is already loaded and running, so the
// editor works on it directly: the game keeps rendering underneath and every
// edit is visible the moment it is made. That also means the editor never has
// to know how to read a scene, only how to write one back out.

bool SceneEditor::Active = false;
bool SceneEditor::ShowGrid = true;
bool SceneEditor::UnsavedChanges = false;

enum SceneEditorTool {
    TOOL_PAINT,
    TOOL_ERASE,
    TOOL_PICK,
    TOOL_SELECT,
    TOOL_STAMP,
    TOOL_ENTITY,
    TOOL_COUNT
};

static const char* const ToolNames[TOOL_COUNT] = {
    "Paint", "Erase", "Pick", "Select", "Stamp", "Entity"
};

// One tile that changed, remembered both ways so it can be undone and redone.
struct TileEdit {
    int    Layer;
    int    X;
    int    Y;
    Uint32 Before;
    Uint32 After;
};

struct EntityMove {
    Entity* Target;
    float   BeforeX, BeforeY;
    float   AfterX, AfterY;
};

// A single undoable action. A drag of the brush across the scene is one of
// these, however many tiles it touched.
struct EditorCommand {
    vector<TileEdit> Tiles;
    vector<EntityMove> Moves;
};

// A rectangle of tiles lifted out of a layer, which can then be stamped down
// elsewhere. HatchStudio calls these Stamps and keeps a collection of them.
struct EditorStamp {
    std::string Name;
    int Width = 0;
    int Height = 0;
    vector<Uint32> Tiles;
};

static int    CurrentTool = TOOL_PAINT;
static int    FocusedLayer = 0;
static Uint32 SelectedTile = 0;
static bool   BrushFlipX = false;
static bool   BrushFlipY = false;
static int    BrushSize = 1;

// Collision planes written along with the tile. -1 leaves whatever is there.
static int    PaintCollisionA = -1;
static int    PaintCollisionB = -1;
static bool   ShowCollision = false;

// Rectangle selection, in tile coordinates on the focused layer.
static bool   HasSelection = false;
static int    SelectionX = 0, SelectionY = 0, SelectionW = 0, SelectionH = 0;
static bool   DraggingSelection = false;
static int    DragStartX = 0, DragStartY = 0;

static vector<EditorStamp> Stamps;
static int    SelectedStamp = -1;

static vector<EditorCommand> UndoStack;
static vector<EditorCommand> RedoStack;
static EditorCommand         PendingCommand;
static bool   BuildingCommand = false;

// The scene view keeps no idea of its own about what is selected: it reads and
// writes the editor's shared Selection, so picking an entity here shows up in
// the hierarchy and the inspector the same frame, and picking one there lights
// it up in the view.
static Entity* SelectedEntity() { return Selection::GetEntity(); }
static bool    DraggingEntity = false;
static float   EntityGrabX = 0.0f, EntityGrabY = 0.0f;

static int    HoverTileX = 0, HoverTileY = 0;
static bool   HoverValid = false;

static char   StatusText[256] = "";

#define SCENE_EDITOR_MAX_UNDO 128

PUBLIC STATIC void SceneEditor::SetStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(StatusText, sizeof(StatusText), format, args);
    va_end(args);
}

PUBLIC STATIC const char* SceneEditor::GetStatus() {
    return StatusText;
}

PUBLIC STATIC bool SceneEditor::HasScene() {
    return Scene::Layers.size() > 0;
}

// --------------------------------------------------------------- viewport ---

// Where the scene's view lands inside the window, matching how Scene::Render
// blits a single view: fitted to the window, keeping its aspect ratio.
PRIVATE STATIC void SceneEditor::GetViewportRect(float* outX, float* outY, float* outW, float* outH) {
    float windowW = (float)UIDraw::ViewWidth;
    float windowH = (float)UIDraw::ViewHeight;

    View* view = &Scene::Views[0];
    float viewW = view->Width > 0.0f ? view->Width : windowW;
    float viewH = view->Height > 0.0f ? view->Height : windowH;

    float outWidth, outHeight;
    if (windowW / viewW < windowH / viewH) {
        outWidth = windowW;
        outHeight = windowW * viewH / viewW;
    }
    else {
        outWidth = windowH * viewW / viewH;
        outHeight = windowH;
    }

    *outX = (windowW - outWidth) * 0.5f;
    *outY = (windowH - outHeight) * 0.5f;
    *outW = outWidth;
    *outH = outHeight;
}

PRIVATE STATIC void SceneEditor::ScreenToWorld(float screenX, float screenY, float* worldX, float* worldY) {
    float x, y, w, h;
    SceneEditor::GetViewportRect(&x, &y, &w, &h);

    View* view = &Scene::Views[0];

    *worldX = view->X + (screenX - x) * (view->Width / (w > 0.0f ? w : 1.0f));
    *worldY = view->Y + (screenY - y) * (view->Height / (h > 0.0f ? h : 1.0f));
}

PRIVATE STATIC void SceneEditor::WorldToScreen(float worldX, float worldY, float* screenX, float* screenY) {
    float x, y, w, h;
    SceneEditor::GetViewportRect(&x, &y, &w, &h);

    View* view = &Scene::Views[0];

    *screenX = x + (worldX - view->X) * (w / (view->Width > 0.0f ? view->Width : 1.0f));
    *screenY = y + (worldY - view->Y) * (h / (view->Height > 0.0f ? view->Height : 1.0f));
}

PRIVATE STATIC float SceneEditor::GetWorldScale() {
    float x, y, w, h;
    SceneEditor::GetViewportRect(&x, &y, &w, &h);

    View* view = &Scene::Views[0];
    return view->Width > 0.0f ? w / view->Width : 1.0f;
}

// ------------------------------------------------------------------ tiles ---

PRIVATE STATIC bool SceneEditor::IsLayerValid(int index) {
    return index >= 0 && index < (int)Scene::Layers.size();
}

PRIVATE STATIC Uint32* SceneEditor::GetTileAddress(int layerIndex, int x, int y) {
    if (!SceneEditor::IsLayerValid(layerIndex))
        return NULL;

    SceneLayer& layer = Scene::Layers[layerIndex];
    if (x < 0 || y < 0 || x >= layer.Width || y >= layer.Height || !layer.Tiles)
        return NULL;

    return &layer.Tiles[x + (y << layer.WidthInBits)];
}

// Writes a tile and files the change with the action being built, so a whole
// brush stroke undoes in one step.
PRIVATE STATIC void SceneEditor::WriteTile(int layerIndex, int x, int y, Uint32 value) {
    Uint32* tile = SceneEditor::GetTileAddress(layerIndex, x, y);
    if (!tile || *tile == value)
        return;

    TileEdit edit;
    edit.Layer = layerIndex;
    edit.X = x;
    edit.Y = y;
    edit.Before = *tile;
    edit.After = value;

    PendingCommand.Tiles.push_back(edit);

    *tile = value;

    // The layer keeps a pristine copy that a scene restart copies back over the
    // live tiles; without updating it too, restarting would undo the edit.
    SceneLayer& layer = Scene::Layers[layerIndex];
    if (layer.TilesBackup)
        layer.TilesBackup[x + (y << layer.WidthInBits)] = value;

    SceneEditor::UnsavedChanges = true;
}

// Builds the tile word the brush paints: the chosen tile, its flip flags, and
// whichever collision planes are being forced.
PRIVATE STATIC Uint32 SceneEditor::MakeBrushTile(Uint32 existing) {
    Uint32 value = SelectedTile & TILE_IDENT_MASK;

    if (BrushFlipX)
        value |= TILE_FLIPX_MASK;
    if (BrushFlipY)
        value |= TILE_FLIPY_MASK;

    if (PaintCollisionA >= 0)
        value |= ((Uint32)PaintCollisionA << 28) & TILE_COLLA_MASK;
    else
        value |= existing & TILE_COLLA_MASK;

    if (PaintCollisionB >= 0)
        value |= ((Uint32)PaintCollisionB << 26) & TILE_COLLB_MASK;
    else
        value |= existing & TILE_COLLB_MASK;

    return value;
}

PRIVATE STATIC void SceneEditor::PaintAt(int tileX, int tileY) {
    int half = BrushSize / 2;

    for (int y = 0; y < BrushSize; y++) {
        for (int x = 0; x < BrushSize; x++) {
            int px = tileX - half + x;
            int py = tileY - half + y;

            Uint32* existing = SceneEditor::GetTileAddress(FocusedLayer, px, py);
            if (!existing)
                continue;

            SceneEditor::WriteTile(FocusedLayer, px, py, SceneEditor::MakeBrushTile(*existing));
        }
    }
}

PRIVATE STATIC void SceneEditor::EraseAt(int tileX, int tileY) {
    int half = BrushSize / 2;

    for (int y = 0; y < BrushSize; y++) {
        for (int x = 0; x < BrushSize; x++)
            SceneEditor::WriteTile(FocusedLayer, tileX - half + x, tileY - half + y, Scene::EmptyTile);
    }
}

// ----------------------------------------------------------------- undo ----

PRIVATE STATIC void SceneEditor::BeginCommand() {
    PendingCommand.Tiles.clear();
    PendingCommand.Moves.clear();
    BuildingCommand = true;
}

PRIVATE STATIC void SceneEditor::EndCommand() {
    BuildingCommand = false;

    if (!PendingCommand.Tiles.size() && !PendingCommand.Moves.size())
        return;

    UndoStack.push_back(PendingCommand);
    RedoStack.clear();

    while (UndoStack.size() > SCENE_EDITOR_MAX_UNDO)
        UndoStack.erase(UndoStack.begin());

    PendingCommand.Tiles.clear();
    PendingCommand.Moves.clear();
}

// Puts a tile back without recording it, since undo and redo are bookkeeping
// rather than edits of their own.
PRIVATE STATIC void SceneEditor::ApplyTileDirect(int layerIndex, int x, int y, Uint32 value) {
    Uint32* tile = SceneEditor::GetTileAddress(layerIndex, x, y);
    if (!tile)
        return;

    *tile = value;

    SceneLayer& layer = Scene::Layers[layerIndex];
    if (layer.TilesBackup)
        layer.TilesBackup[x + (y << layer.WidthInBits)] = value;
}

PUBLIC STATIC void SceneEditor::Undo() {
    if (!UndoStack.size()) {
        SceneEditor::SetStatus("Nothing to undo.");
        return;
    }

    EditorCommand command = UndoStack.back();
    UndoStack.pop_back();

    for (size_t i = 0; i < command.Tiles.size(); i++) {
        TileEdit& edit = command.Tiles[i];
        SceneEditor::ApplyTileDirect(edit.Layer, edit.X, edit.Y, edit.Before);
    }

    for (size_t i = 0; i < command.Moves.size(); i++) {
        EntityMove& move = command.Moves[i];
        move.Target->X = move.BeforeX;
        move.Target->Y = move.BeforeY;
    }

    RedoStack.push_back(command);

    SceneEditor::UnsavedChanges = true;
    SceneEditor::SetStatus("Undid %d tile change(s).", (int)command.Tiles.size());
}

PUBLIC STATIC void SceneEditor::Redo() {
    if (!RedoStack.size()) {
        SceneEditor::SetStatus("Nothing to redo.");
        return;
    }

    EditorCommand command = RedoStack.back();
    RedoStack.pop_back();

    for (size_t i = 0; i < command.Tiles.size(); i++) {
        TileEdit& edit = command.Tiles[i];
        SceneEditor::ApplyTileDirect(edit.Layer, edit.X, edit.Y, edit.After);
    }

    for (size_t i = 0; i < command.Moves.size(); i++) {
        EntityMove& move = command.Moves[i];
        move.Target->X = move.AfterX;
        move.Target->Y = move.AfterY;
    }

    UndoStack.push_back(command);

    SceneEditor::UnsavedChanges = true;
    SceneEditor::SetStatus("Redid %d tile change(s).", (int)command.Tiles.size());
}

PUBLIC STATIC void SceneEditor::ClearHistory() {
    UndoStack.clear();
    RedoStack.clear();
    PendingCommand.Tiles.clear();
    PendingCommand.Moves.clear();
    BuildingCommand = false;
}

// ------------------------------------------------------------------ save ---

// Where the scene that is loaded lives on disk. Only meaningful when the game
// is being read out of a Resources folder rather than a packed data file.
PUBLIC STATIC bool SceneEditor::GetScenePath(char* out, size_t outSize) {
    if (!Scene::CurrentScene[0])
        return false;

    if (!ResourceManager::UsingDataFolder)
        return false;

    snprintf(out, outSize, "Resources/%s", Scene::CurrentScene);
    return true;
}

// True when this scene is in a format the editor knows how to write back.
PUBLIC STATIC bool SceneEditor::CanSave() {
    char path[1024];
    if (!SceneEditor::GetScenePath(path, sizeof(path)))
        return false;

    return StringUtils::StrCaseStr(path, ".tmx") != NULL;
}

PUBLIC STATIC bool SceneEditor::Save() {
    char path[1024];
    if (!SceneEditor::GetScenePath(path, sizeof(path))) {
        SceneEditor::SetStatus("Scenes inside a packed .hatch file cannot be saved.");
        return false;
    }

    if (!SceneEditor::CanSave()) {
        SceneEditor::SetStatus("Only Tiled .tmx scenes can be written back so far.");
        return false;
    }

    if (!TiledMapWriter::Write(path, path)) {
        SceneEditor::SetStatus("Could not save \"%s\".", path);
        return false;
    }

    SceneEditor::UnsavedChanges = false;
    SceneEditor::SetStatus("Saved %s.", Scene::CurrentScene);

    return true;
}

// ---------------------------------------------------------------- stamps ---

PRIVATE STATIC void SceneEditor::CaptureStamp() {
    if (!HasSelection || SelectionW <= 0 || SelectionH <= 0)
        return;

    EditorStamp stamp;
    stamp.Width = SelectionW;
    stamp.Height = SelectionH;
    stamp.Tiles.reserve((size_t)SelectionW * SelectionH);

    for (int y = 0; y < SelectionH; y++) {
        for (int x = 0; x < SelectionW; x++) {
            Uint32* tile = SceneEditor::GetTileAddress(FocusedLayer, SelectionX + x, SelectionY + y);
            stamp.Tiles.push_back(tile ? *tile : Scene::EmptyTile);
        }
    }

    char name[64];
    snprintf(name, sizeof(name), "Stamp %d (%dx%d)", (int)Stamps.size() + 1, SelectionW, SelectionH);
    stamp.Name = name;

    Stamps.push_back(stamp);
    SelectedStamp = (int)Stamps.size() - 1;

    SceneEditor::SetStatus("Captured %dx%d stamp.", SelectionW, SelectionH);
}

// Drops a stamp with its top-left corner at the given tile. Empty tiles in the
// stamp are skipped, so stamps can be laid over existing artwork.
PRIVATE STATIC void SceneEditor::PlaceStamp(int tileX, int tileY) {
    if (SelectedStamp < 0 || SelectedStamp >= (int)Stamps.size())
        return;

    EditorStamp& stamp = Stamps[SelectedStamp];

    for (int y = 0; y < stamp.Height; y++) {
        for (int x = 0; x < stamp.Width; x++) {
            Uint32 value = stamp.Tiles[y * stamp.Width + x];
            if ((value & TILE_IDENT_MASK) == Scene::EmptyTile)
                continue;

            SceneEditor::WriteTile(FocusedLayer, tileX + x, tileY + y, value);
        }
    }
}

// -------------------------------------------------------------- entities ---

// The entity whose position is nearest the given world point, within a few
// tiles of it.
PRIVATE STATIC Entity* SceneEditor::PickEntity(float worldX, float worldY) {
    Entity* best = NULL;
    float bestDistance = (float)(Scene::TileWidth * 2);

    for (Entity* entity = Scene::ObjectFirst; entity; entity = entity->NextEntity) {
        float dx = entity->X - worldX;
        float dy = entity->Y - worldY;
        float distance = sqrtf(dx * dx + dy * dy);

        if (distance < bestDistance) {
            bestDistance = distance;
            best = entity;
        }
    }

    return best;
}

// ----------------------------------------------------------------- input ---

// Handles the mouse over the scene itself. The dock panels have already had
// their say, so this only runs when the pointer is out over the world.
PRIVATE STATIC void SceneEditor::UpdateViewportInput(float areaX, float areaY, float areaW, float areaH) {
    HoverValid = false;

    if (!SceneEditor::HasScene() || !SceneEditor::IsLayerValid(FocusedLayer))
        return;

    // Anything outside the world area belongs to the panels around it.
    if (UICore::MouseX < areaX || UICore::MouseX >= areaX + areaW ||
        UICore::MouseY < areaY || UICore::MouseY >= areaY + areaH)
        return;

    float worldX, worldY;
    SceneEditor::ScreenToWorld(UICore::MouseX, UICore::MouseY, &worldX, &worldY);

    SceneLayer& layer = Scene::Layers[FocusedLayer];

    int tileX = (int)floorf((worldX - layer.OffsetX) / Scene::TileWidth);
    int tileY = (int)floorf((worldY - layer.OffsetY) / Scene::TileHeight);

    HoverTileX = tileX;
    HoverTileY = tileY;
    HoverValid = true;

    bool inside = tileX >= 0 && tileY >= 0 && tileX < layer.Width && tileY < layer.Height;

    switch (CurrentTool) {
        case TOOL_PAINT:
        case TOOL_ERASE:
            if (UICore::MouseWasPressed && inside)
                SceneEditor::BeginCommand();

            // A press and its release can both arrive before a frame is drawn,
            // so the tile under the pointer is laid down as soon as the stroke
            // starts; waiting for the button to still be held would drop every
            // quick click.
            if (BuildingCommand && inside &&
                (UICore::MouseWasPressed || UICore::MouseIsDown)) {
                if (CurrentTool == TOOL_PAINT)
                    SceneEditor::PaintAt(tileX, tileY);
                else
                    SceneEditor::EraseAt(tileX, tileY);
            }

            if (UICore::MouseWasReleased && BuildingCommand)
                SceneEditor::EndCommand();
            break;

        case TOOL_PICK:
            if (UICore::MouseWasPressed && inside) {
                Uint32* tile = SceneEditor::GetTileAddress(FocusedLayer, tileX, tileY);
                if (tile) {
                    SelectedTile = *tile & TILE_IDENT_MASK;
                    BrushFlipX = (*tile & TILE_FLIPX_MASK) != 0;
                    BrushFlipY = (*tile & TILE_FLIPY_MASK) != 0;
                    SceneEditor::SetStatus("Picked tile %u.", SelectedTile);
                }
            }
            break;

        case TOOL_SELECT:
            if (UICore::MouseWasPressed && inside) {
                DraggingSelection = true;
                DragStartX = tileX;
                DragStartY = tileY;
            }

            if (DraggingSelection) {
                SelectionX = tileX < DragStartX ? tileX : DragStartX;
                SelectionY = tileY < DragStartY ? tileY : DragStartY;
                SelectionW = abs(tileX - DragStartX) + 1;
                SelectionH = abs(tileY - DragStartY) + 1;
                HasSelection = true;
            }

            if (UICore::MouseWasReleased && DraggingSelection) {
                DraggingSelection = false;
                SceneEditor::SetStatus("Selected %dx%d tiles.", SelectionW, SelectionH);
            }
            break;

        case TOOL_STAMP:
            if (UICore::MouseWasPressed && inside) {
                SceneEditor::BeginCommand();
                SceneEditor::PlaceStamp(tileX, tileY);
                SceneEditor::EndCommand();
            }
            break;

        case TOOL_ENTITY:
            if (UICore::MouseWasPressed) {
                Entity* picked = SceneEditor::PickEntity(worldX, worldY);
                if (picked) {
                    Selection::SetEntity(picked);
                    DraggingEntity = true;
                    EntityGrabX = picked->X;
                    EntityGrabY = picked->Y;
                }
                else
                    Selection::SetEntity(NULL);
            }

            if (DraggingEntity && SelectedEntity() && UICore::MouseIsDown) {
                SelectedEntity()->X = worldX;
                SelectedEntity()->Y = worldY;
            }

            if (UICore::MouseWasReleased && DraggingEntity && SelectedEntity()) {
                DraggingEntity = false;

                if (SelectedEntity()->X != EntityGrabX || SelectedEntity()->Y != EntityGrabY) {
                    EntityMove move;
                    move.Target = SelectedEntity();
                    move.BeforeX = EntityGrabX;
                    move.BeforeY = EntityGrabY;
                    move.AfterX = SelectedEntity()->X;
                    move.AfterY = SelectedEntity()->Y;

                    SceneEditor::BeginCommand();
                    PendingCommand.Moves.push_back(move);
                    SceneEditor::EndCommand();

                    SceneEditor::UnsavedChanges = true;
                }
            }
            break;
    }
}

// -------------------------------------------------------------- overlays ---

// Grid, selection, brush preview and entity markers, drawn over the scene the
// engine has already rendered.
PRIVATE STATIC void SceneEditor::DrawViewportOverlay(float areaX, float areaY, float areaW, float areaH) {
    if (!SceneEditor::HasScene() || !SceneEditor::IsLayerValid(FocusedLayer))
        return;

    SceneLayer& layer = Scene::Layers[FocusedLayer];
    float scale = SceneEditor::GetWorldScale();

    float tileW = Scene::TileWidth * scale;
    float tileH = Scene::TileHeight * scale;

    // Everything below is clipped to the world area so the grid and the markers
    // cannot spill over the menu bar, the tabs or the dock.
    UIDraw::PushClip(areaX, areaY, areaW, areaH);

    // The grid is only worth drawing when the tiles are big enough on screen
    // to tell the lines apart.
    if (SceneEditor::ShowGrid && tileW >= 4.0f && tileH >= 4.0f) {
        float originX, originY;
        SceneEditor::WorldToScreen((float)layer.OffsetX, (float)layer.OffsetY, &originX, &originY);

        int firstX = 0, firstY = 0;
        if (originX < 0.0f)
            firstX = (int)(-originX / tileW);
        if (originY < 0.0f)
            firstY = (int)(-originY / tileH);

        for (int x = firstX; x <= layer.Width; x++) {
            float lineX = originX + x * tileW;
            if (lineX < 0.0f)
                continue;
            if (lineX > areaX + areaW)
                break;

            UIDraw::FillRect(lineX, originY, 1.0f, layer.Height * tileH, 0x30FFFFFF);
        }

        for (int y = firstY; y <= layer.Height; y++) {
            float lineY = originY + y * tileH;
            if (lineY < 0.0f)
                continue;
            if (lineY > areaY + areaH)
                break;

            UIDraw::FillRect(originX, lineY, layer.Width * tileW, 1.0f, 0x30FFFFFF);
        }
    }

    // Collision planes, as a wash over any tile that has them.
    if (ShowCollision) {
        for (int y = 0; y < layer.Height; y++) {
            for (int x = 0; x < layer.Width; x++) {
                Uint32* tile = SceneEditor::GetTileAddress(FocusedLayer, x, y);
                if (!tile || !(*tile & (TILE_COLLA_MASK | TILE_COLLB_MASK)))
                    continue;

                float screenX, screenY;
                SceneEditor::WorldToScreen((float)(layer.OffsetX + x * Scene::TileWidth),
                    (float)(layer.OffsetY + y * Scene::TileHeight), &screenX, &screenY);

                if (screenX + tileW < areaX || screenY + tileH < areaY ||
                    screenX > areaX + areaW || screenY > areaY + areaH)
                    continue;

                Uint32 color = (*tile & TILE_COLLA_MASK) ? 0x5040FF40 : 0x504080FF;
                UIDraw::FillRect(screenX, screenY, tileW, tileH, color);
            }
        }
    }

    // Selection.
    if (HasSelection) {
        float screenX, screenY;
        SceneEditor::WorldToScreen((float)(layer.OffsetX + SelectionX * Scene::TileWidth),
            (float)(layer.OffsetY + SelectionY * Scene::TileHeight), &screenX, &screenY);

        UIDraw::FillRect(screenX, screenY, SelectionW * tileW, SelectionH * tileH, 0x304FA3FF);
        UIDraw::StrokeRect(screenX, screenY, SelectionW * tileW, SelectionH * tileH, UI_COL_ACCENT);
    }

    // Where the brush would land.
    if (HoverValid && (CurrentTool == TOOL_PAINT || CurrentTool == TOOL_ERASE || CurrentTool == TOOL_STAMP)) {
        int spanX = BrushSize;
        int spanY = BrushSize;
        int originTileX = HoverTileX - BrushSize / 2;
        int originTileY = HoverTileY - BrushSize / 2;

        if (CurrentTool == TOOL_STAMP && SelectedStamp >= 0 && SelectedStamp < (int)Stamps.size()) {
            spanX = Stamps[SelectedStamp].Width;
            spanY = Stamps[SelectedStamp].Height;
            originTileX = HoverTileX;
            originTileY = HoverTileY;
        }

        float screenX, screenY;
        SceneEditor::WorldToScreen((float)(layer.OffsetX + originTileX * Scene::TileWidth),
            (float)(layer.OffsetY + originTileY * Scene::TileHeight), &screenX, &screenY);

        UIDraw::StrokeRect(screenX, screenY, spanX * tileW, spanY * tileH,
            CurrentTool == TOOL_ERASE ? UI_COL_DANGER : UI_COL_SUCCESS);
    }

    // Entity markers.
    if (CurrentTool == TOOL_ENTITY) {
        for (Entity* entity = Scene::ObjectFirst; entity; entity = entity->NextEntity) {
            float screenX, screenY;
            SceneEditor::WorldToScreen(entity->X, entity->Y, &screenX, &screenY);

            if (screenX < areaX || screenY < areaY ||
                screenX > areaX + areaW || screenY > areaY + areaH)
                continue;

            float size = 3.0f * UIDraw::Scale;
            Uint32 color = entity == SelectedEntity() ? UI_COL_ACCENT : UI_COL_WARNING;

            UIDraw::FillRect(screenX - size, screenY - 1.0f, size * 2.0f, 2.0f, color);
            UIDraw::FillRect(screenX - 1.0f, screenY - size, 2.0f, size * 2.0f, color);
        }
    }

    UIDraw::PopClip();
}

// ----------------------------------------------------------------- panels ---

// The tile palette. Tiles are drawn with the engine's own sprite call so they
// look exactly as they will in the scene.
PRIVATE STATIC void SceneEditor::DrawTilePalette(float width) {
    size_t tileCount = Scene::TileSpriteInfos.size();
    if (!tileCount) {
        UICore::Text("This scene has no tileset.", UI_COL_TEXT_FAINT);
        return;
    }

    float cell = 20.0f * UIDraw::Scale;
    int columns = (int)(width / cell);
    if (columns < 1)
        columns = 1;

    int rows = (int)((tileCount + columns - 1) / columns);

    float x, y;
    UICore::PlaceCustomItem(width, rows * cell, &x, &y);

    for (size_t i = 0; i < tileCount; i++) {
        float cellX = x + (i % columns) * cell;
        float cellY = y + (i / columns) * cell;

        if (UIDraw::IsClipped(cellX, cellY, cell, cell))
            continue;

        // Drawn straight from the tileset sheet rather than through
        // Graphics::DrawSprite, which on the OpenGL backend renders a
        // pre-built buffer per frame at its natural size and would ignore the
        // scaling the palette needs.
        // Tiles are textures rather than flat colour, and the blend colour is
        // shared state that the highlight rectangles below overwrite, so it has
        // to go back to white for every one of them.
        Graphics::SetBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

        TileSpriteInfo& info = Scene::TileSpriteInfos[i];
        if (info.Sprite && Graphics::SpriteRangeCheck(info.Sprite, info.AnimationIndex, info.FrameIndex) == false) {
            AnimFrame& frame = info.Sprite->Animations[info.AnimationIndex].Frames[info.FrameIndex];
            Texture* sheet = info.Sprite->Spritesheets[frame.SheetNumber];

            if (sheet && frame.Width > 0 && frame.Height > 0)
                Graphics::DrawTexture(sheet,
                    (float)frame.X, (float)frame.Y, (float)frame.Width, (float)frame.Height,
                    cellX, cellY, cell, cell);
        }

        if (UICore::IsOver(cellX, cellY, cell, cell)) {
            UIDraw::StrokeRect(cellX, cellY, cell, cell, UI_COL_TEXT);

            if (UICore::MouseWasPressed) {
                SelectedTile = (Uint32)i;
                SceneEditor::SetStatus("Selected tile %u.", SelectedTile);
            }
        }

        if (i == SelectedTile)
            UIDraw::StrokeRect(cellX, cellY, cell, cell, UI_COL_ACCENT);
    }
}

PRIVATE STATIC void SceneEditor::DrawToolsPanel() {
    UICore::Field("Scene", Scene::CurrentScene[0] ? Scene::CurrentScene : "(none)");

    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    if (UICore::ButtonEnabled("Save Scene", SceneEditor::CanSave()))
        SceneEditor::Save();
    UICore::Tooltip("Rewrites the tile layers in the scene file.");

    UICore::SameLine();
    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    UICore::Text(SceneEditor::UnsavedChanges ? "unsaved changes" : "no changes",
        SceneEditor::UnsavedChanges ? UI_COL_WARNING : UI_COL_TEXT_FAINT);

    UICore::Separator();
    UICore::Heading("Tool");

    float buttonWidth = UICore::ContentWidth() / 3.0f - UICore::Pad();
    for (int i = 0; i < TOOL_COUNT; i++) {
        char label[64];
        snprintf(label, sizeof(label), "%s##tool%d", ToolNames[i], i);

        UICore::SetNextItemWidth(buttonWidth);

        // The chosen tool is shown greyed out, which reads as pressed in and
        // stops it being picked again.
        if (CurrentTool == i)
            UICore::ButtonEnabled(label, false);
        else if (UICore::Button(label))
            CurrentTool = i;

        if (i % 3 != 2)
            UICore::SameLine();
    }

    UICore::Separator();

    UICore::SliderInt("Brush size", &BrushSize, 1, 8);
    UICore::Checkbox("Flip X", &BrushFlipX);
    UICore::Checkbox("Flip Y", &BrushFlipY);
    UICore::Checkbox("Show grid", &SceneEditor::ShowGrid);
    UICore::Checkbox("Show collision", &ShowCollision);

    UICore::Separator();
    UICore::Heading("Collision When Painting");

    static const char* const CollisionNames[] = { "leave alone", "none", "top only", "sides", "all" };

    int planeA = PaintCollisionA + 1;
    if (UICore::Dropdown("Plane A", CollisionNames, 5, &planeA))
        PaintCollisionA = planeA - 1;

    int planeB = PaintCollisionB + 1;
    if (UICore::Dropdown("Plane B", CollisionNames, 5, &planeB))
        PaintCollisionB = planeB - 1;

    UICore::Separator();

    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    if (UICore::ButtonEnabled("Undo", UndoStack.size() > 0))
        SceneEditor::Undo();

    UICore::SameLine();
    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    if (UICore::ButtonEnabled("Redo", RedoStack.size() > 0))
        SceneEditor::Redo();

    UICore::FieldFormatted("History", "%d undo, %d redo", (int)UndoStack.size(), (int)RedoStack.size());
}

PRIVATE STATIC void SceneEditor::DrawLayersPanel() {
    UICore::Heading("Layers");

    if (!Scene::Layers.size()) {
        UICore::Text("No layers.", UI_COL_TEXT_FAINT);
        return;
    }

    UICore::ResetRowStriping();

    for (size_t i = 0; i < Scene::Layers.size(); i++) {
        SceneLayer& layer = Scene::Layers[i];

        char label[128];
        snprintf(label, sizeof(label), "%s  %dx%d##layerrow%d",
            layer.Name, layer.Width, layer.Height, (int)i);

        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.74f);
        if (UICore::ListItem(label, FocusedLayer == (int)i))
            FocusedLayer = (int)i;

        UICore::SameLine();
        UICore::SetNextItemWidth(UICore::ContentWidth() * 0.24f);

        char visibleLabel[32];
        snprintf(visibleLabel, sizeof(visibleLabel), "shown##layervis%d", (int)i);

        bool visible = layer.Visible;
        if (UICore::Checkbox(visibleLabel, &visible))
            layer.Visible = visible;
    }

    if (SceneEditor::IsLayerValid(FocusedLayer)) {
        SceneLayer& layer = Scene::Layers[FocusedLayer];

        UICore::Separator();
        UICore::Heading("Focused Layer");
        UICore::Field("Name", layer.Name);
        UICore::FieldFormatted("Size", "%d x %d tiles", layer.Width, layer.Height);
        UICore::FieldFormatted("Offset", "%d, %d", layer.OffsetX, layer.OffsetY);
        UICore::FieldFormatted("Draw group", "%d", layer.DrawGroup);
        UICore::FieldFormatted("Collideable", "%s",
            (layer.Flags & SceneLayer::FLAGS_COLLIDEABLE) ? "yes" : "no");
    }
}

PRIVATE STATIC void SceneEditor::DrawStampsPanel() {
    UICore::Heading("Stamps");

    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    if (UICore::ButtonEnabled("Capture", HasSelection))
        SceneEditor::CaptureStamp();
    UICore::Tooltip("Lifts the selected tiles out as a reusable stamp.");

    UICore::SameLine();
    UICore::SetNextItemWidth(UICore::ContentWidth() * 0.48f);
    if (UICore::ButtonEnabled("Clear List", Stamps.size() > 0)) {
        Stamps.clear();
        SelectedStamp = -1;
    }

    if (!Stamps.size()) {
        UICore::Text("Select tiles with the Select tool,", UI_COL_TEXT_FAINT);
        UICore::Text("then Capture to make a stamp.", UI_COL_TEXT_FAINT);
        return;
    }

    UICore::ResetRowStriping();
    for (size_t i = 0; i < Stamps.size(); i++) {
        char label[128];
        snprintf(label, sizeof(label), "%s##stamp%d", Stamps[i].Name.c_str(), (int)i);

        if (UICore::ListItem(label, SelectedStamp == (int)i)) {
            SelectedStamp = (int)i;
            CurrentTool = TOOL_STAMP;
        }
    }
}

PRIVATE STATIC void SceneEditor::DrawEntitiesPanel() {
    UICore::Heading("Entities");
    UICore::FieldFormatted("In scene", "%d", Scene::ObjectCount);

    if (SelectedEntity()) {
        UICore::Separator();
        UICore::Field("Selected", SelectedEntity()->List ? SelectedEntity()->List->ObjectName : "(unknown)");
        UICore::FieldFormatted("Position", "%.1f, %.1f", SelectedEntity()->X, SelectedEntity()->Y);
        UICore::FieldFormatted("Start", "%.1f, %.1f", SelectedEntity()->InitialX, SelectedEntity()->InitialY);

        if (UICore::Button("Snap To Tile Grid")) {
            SceneEditor::BeginCommand();

            EntityMove move;
            move.Target = SelectedEntity();
            move.BeforeX = SelectedEntity()->X;
            move.BeforeY = SelectedEntity()->Y;
            move.AfterX = floorf(SelectedEntity()->X / Scene::TileWidth) * Scene::TileWidth + Scene::TileWidth / 2;
            move.AfterY = floorf(SelectedEntity()->Y / Scene::TileHeight) * Scene::TileHeight + Scene::TileHeight / 2;

            SelectedEntity()->X = move.AfterX;
            SelectedEntity()->Y = move.AfterY;

            PendingCommand.Moves.push_back(move);
            SceneEditor::EndCommand();

            SceneEditor::UnsavedChanges = true;
        }

        if (UICore::Button("Deselect"))
            Selection::SetEntity(NULL);
    }
    else
        UICore::Text("Pick one with the Entity tool.", UI_COL_TEXT_FAINT);
}

// ----------------------------------------------------------------- render ---

PUBLIC STATIC void SceneEditor::Reset() {
    SceneEditor::ClearHistory();

    Stamps.clear();
    SelectedStamp = -1;
    Selection::SetEntity(NULL);
    DraggingEntity = false;
    DraggingSelection = false;
    HasSelection = false;
    FocusedLayer = 0;
    SceneEditor::UnsavedChanges = false;
    StatusText[0] = '\0';
}

// Draws the editor. The scene is already on screen behind it, so only the dock
// on the right and the overlays over the world are painted here.
PUBLIC STATIC void SceneEditor::Draw(float x, float y, float width, float height) {
    float dockWidth = 46.0f * UIDraw::CharWidth();
    if (dockWidth > width * 0.5f)
        dockWidth = width * 0.5f;

    float dockX = x + width - dockWidth;

    if (!SceneEditor::HasScene()) {
        UICore::BeginPanel("Scene Editor", dockX, y, dockWidth, height);
            UICore::Text("No scene is loaded.", UI_COL_TEXT_DIM);
            UICore::Text("Open one from the Scenes tab, then", UI_COL_TEXT_FAINT);
            UICore::Text("come back here to edit it.", UI_COL_TEXT_FAINT);
        UICore::EndPanel();
        return;
    }

    if (FocusedLayer >= (int)Scene::Layers.size())
        FocusedLayer = 0;

    // The world reacts first, then the dock is drawn over it.
    SceneEditor::UpdateViewportInput(x, y, dockX - x, height);
    SceneEditor::DrawViewportOverlay(x, y, dockX - x, height);

    float paletteHeight = height * 0.34f;

    UICore::BeginPanel("Tiles", dockX, y, dockWidth, paletteHeight);
        SceneEditor::DrawTilePalette(UICore::ContentWidth());
    UICore::EndPanel();

    UICore::BeginPanel("Scene Editor", dockX, y + paletteHeight, dockWidth, height - paletteHeight);
        SceneEditor::DrawToolsPanel();
        UICore::Separator();
        SceneEditor::DrawLayersPanel();
        UICore::Separator();
        SceneEditor::DrawStampsPanel();
        UICore::Separator();
        SceneEditor::DrawEntitiesPanel();
    UICore::EndPanel();

    // A readout of where the pointer is, along the bottom of the world area.
    char readout[256];
    if (HoverValid)
        snprintf(readout, sizeof(readout), "tile %d, %d   layer %d   tile %u%s%s",
            HoverTileX, HoverTileY, FocusedLayer, SelectedTile,
            BrushFlipX ? " flipX" : "", BrushFlipY ? " flipY" : "");
    else
        snprintf(readout, sizeof(readout), "%s", "move the pointer over the scene");

    UIDraw::Text(x + UICore::Pad() * 2.0f, y + height - UIDraw::LineHeight() - UICore::Pad(),
        readout, UI_COL_TEXT_DIM);
}
