#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/UI/TileCollisionTypes.h>

class TileCollisionEditor {
public:
    static bool UnsavedChanges;
};
#endif

#include <Engine/UI/TileCollisionEditor.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Graphics.h>
#include <Engine/IO/FileStream.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneInfo.h>
#include <Engine/Scene/TileConfig.h>
#include <Engine/Utilities/StringUtils.h>

// The tile collision editor, after the one HatchStudio keeps beside its scene
// editor: a tile blown up large enough to draw on, where each of the sixteen
// columns is given a height, with an angle and a ceiling flag alongside.
//
// Collision files store exactly those column heights; everything else the
// engine collides against -- which sides are solid, the per-side angles, the
// flipped copies of the tile -- is worked out from them when the file loads.
// This edits the heights and then asks the engine to work the rest out again
// through the same routine the loader uses, so a tile drawn here behaves
// identically to the same tile loaded from a file.

bool TileCollisionEditor::UnsavedChanges = false;

// One set per collision plane. Hatch scenes have two, A and B.
static vector<TileCollisionEntry> Planes[2];
static bool  Loaded = false;

static int   SelectedTile = 0;
static int   CurrentPlane = 0;
static bool  ShowTileImage = true;
static bool  ShowGrid = true;
static bool  Painting = false;
static bool  PaintingClears = false;

static char  StatusText[256] = "";

#define TILE_COLLISION_NONE 0xFF

PRIVATE STATIC void TileCollisionEditor::SetStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(StatusText, sizeof(StatusText), format, args);
    va_end(args);
}

PUBLIC STATIC const char* TileCollisionEditor::GetStatus() {
    return StatusText;
}

PUBLIC STATIC bool TileCollisionEditor::IsAvailable() {
    return Scene::TileCfgLoaded && Scene::TileCfg.size() > 0 && Scene::TileSpriteInfos.size() > 0;
}

// ------------------------------------------------------------------ data ---

// Recovers the column heights from a tile the engine has already loaded. The
// loader turns heights into solid spans, so this undoes that: a floor tile
// keeps its heights in CollisionTop, and a ceiling tile has them flipped into
// CollisionBottom.
PRIVATE STATIC void TileCollisionEditor::ReadEntryFromScene(int plane, int tileID, TileCollisionEntry* entry) {
    TileConfig* tile = &Scene::TileCfg[plane][tileID];

    entry->IsCeiling = tile->IsCeiling;
    entry->HasCollision = false;

    for (int i = 0; i < 16; i++) {
        Uint8 height = TILE_COLLISION_NONE;

        if (tile->IsCeiling) {
            if (tile->CollisionBottom[i] != TILE_COLLISION_NONE)
                height = tile->CollisionBottom[i] ^ 15;
        }
        else if (tile->CollisionTop[i] != TILE_COLLISION_NONE)
            height = tile->CollisionTop[i];

        entry->Heights[i] = height;

        if (height != TILE_COLLISION_NONE)
            entry->HasCollision = true;
    }

    entry->Angle = tile->IsCeiling ? tile->AngleBottom : tile->AngleTop;
}

PUBLIC STATIC void TileCollisionEditor::Reload() {
    Planes[0].clear();
    Planes[1].clear();
    Loaded = false;
    TileCollisionEditor::UnsavedChanges = false;

    if (!TileCollisionEditor::IsAvailable())
        return;

    size_t tileCount = Scene::TileSpriteInfos.size();

    for (int plane = 0; plane < 2 && plane < (int)Scene::TileCfg.size(); plane++) {
        Planes[plane].resize(tileCount);

        for (size_t i = 0; i < tileCount; i++)
            TileCollisionEditor::ReadEntryFromScene(plane, (int)i, &Planes[plane][i]);
    }

    Loaded = true;
}

PUBLIC STATIC void TileCollisionEditor::Invalidate() {
    Loaded = false;
}

// Pushes one tile's edited heights back into the live scene, so the change is
// felt by anything colliding with it straight away.
PRIVATE STATIC void TileCollisionEditor::ApplyToScene(int plane, int tileID) {
    if (plane >= (int)Scene::TileCfg.size() || tileID >= (int)Scene::TileSpriteInfos.size())
        return;

    TileCollisionEntry& entry = Planes[plane][tileID];
    TileConfig* tile = &Scene::TileCfg[plane][tileID];

    tile->IsCeiling = entry.IsCeiling;
    tile->AngleTop = entry.Angle;

    // The loader hands the derivation a buffer of heights, with anything at or
    // above the tile size meaning "no collision in this column".
    Uint8 heights[16];
    for (int i = 0; i < 16; i++)
        heights[i] = entry.Heights[i] == TILE_COLLISION_NONE ? 16 : entry.Heights[i];

    Scene::ApplyTileCollisionHeights(tile, heights, entry.HasCollision, 16);

    TileCollisionEditor::UnsavedChanges = true;
}

PRIVATE STATIC void TileCollisionEditor::RefreshHasCollision(TileCollisionEntry* entry) {
    entry->HasCollision = false;

    for (int i = 0; i < 16; i++) {
        if (entry->Heights[i] != TILE_COLLISION_NONE) {
            entry->HasCollision = true;
            return;
        }
    }
}

// Gives one column its surface, or takes the surface away again if the stroke
// started on a column that already reached that far.
PRIVATE STATIC void TileCollisionEditor::PaintColumn(TileCollisionEntry* entry, int column, int row) {
    entry->Heights[column] = PaintingClears ? TILE_COLLISION_NONE
        : (Uint8)(entry->IsCeiling ? 15 - row : row);

    TileCollisionEditor::RefreshHasCollision(entry);
    TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
}

// ------------------------------------------------------------------ save ---

PUBLIC STATIC bool TileCollisionEditor::GetCollisionPath(char* out, size_t outSize) {
    if (!ResourceManager::UsingDataFolder)
        return false;
    if (!SceneInfo::IsEntryValid(Scene::CurrentSceneInList))
        return false;

    std::string filename = SceneInfo::GetTileConfigFilename(Scene::CurrentSceneInList);
    snprintf(out, outSize, "Resources/%s", filename.c_str());

    return true;
}

PUBLIC STATIC bool TileCollisionEditor::CanSave() {
    char path[1024];
    return Loaded && TileCollisionEditor::GetCollisionPath(path, sizeof(path));
}

// Writes the collision file back in the format the engine's HCOL reader
// expects: a short header, then a ceiling flag, an angle, a collision flag and
// sixteen column heights for every tile.
PUBLIC STATIC bool TileCollisionEditor::Save() {
    char path[1024];
    if (!TileCollisionEditor::GetCollisionPath(path, sizeof(path))) {
        TileCollisionEditor::SetStatus("This scene has nowhere to write tile collision to.");
        return false;
    }

    Stream* writer = FileStream::New(path, FileStream::WRITE_ACCESS);
    if (!writer) {
        TileCollisionEditor::SetStatus("Could not write \"%s\".", path);
        return false;
    }

    // A collision file covers one tileset, and its first entry is that
    // tileset's first tile rather than tile zero of the scene.
    size_t tileStart = Scene::Tilesets.size() ? Scene::Tilesets[0].StartTile : 0;
    size_t tileCount = Scene::Tilesets.size() ? Scene::Tilesets[0].TileCount - 1 : Planes[0].size();

    if (tileStart + tileCount > Planes[0].size())
        tileCount = Planes[0].size() - tileStart;

    writer->WriteUInt32(0x4C4F4354U); // "TCOL"
    writer->WriteUInt32((Uint32)tileCount);
    writer->WriteByte(16);            // tile size
    writer->WriteByte(0);
    writer->WriteByte(0);
    writer->WriteByte(0);
    writer->WriteUInt32(0);

    for (size_t i = 0; i < tileCount; i++) {
        TileCollisionEntry& entry = Planes[0][tileStart + i];

        writer->WriteByte(entry.IsCeiling);
        writer->WriteByte(entry.Angle);
        writer->WriteByte(entry.HasCollision ? 1 : 0);

        for (int c = 0; c < 16; c++)
            writer->WriteByte(entry.Heights[c] == TILE_COLLISION_NONE ? 16 : entry.Heights[c]);
    }

    writer->Close();

    TileCollisionEditor::UnsavedChanges = false;
    TileCollisionEditor::SetStatus("Saved tile collision to %s.", path);

    Log::Print(Log::LOG_IMPORTANT, "Saved %d tile collisions to \"%s\".", (int)tileCount, path);

    return true;
}

// ---------------------------------------------------------------- drawing --

// The magnified tile. Each of the sixteen columns is drawn as a solid bar down
// from the height it was given, or up from the bottom for a ceiling tile.
PRIVATE STATIC void TileCollisionEditor::DrawTileCanvas(float width) {
    if (SelectedTile < 0 || SelectedTile >= (int)Planes[CurrentPlane].size())
        return;

    float side = width;
    if (side > UIDraw::ViewHeight * 0.6f)
        side = UIDraw::ViewHeight * 0.6f;

    float cell = floorf(side / 16.0f);
    if (cell < 1.0f)
        cell = 1.0f;
    side = cell * 16.0f;

    float x, y;
    UICore::PlaceCustomItem(side, side, &x, &y);

    TileCollisionEntry& entry = Planes[CurrentPlane][SelectedTile];

    // The tile's own graphic underneath, so collision can be drawn against what
    // it is meant to match.
    if (ShowTileImage) {
        Graphics::SetBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

        TileSpriteInfo& info = Scene::TileSpriteInfos[SelectedTile];
        if (info.Sprite && !Graphics::SpriteRangeCheck(info.Sprite, info.AnimationIndex, info.FrameIndex)) {
            AnimFrame& frame = info.Sprite->Animations[info.AnimationIndex].Frames[info.FrameIndex];
            Texture* sheet = info.Sprite->Spritesheets[frame.SheetNumber];

            if (sheet && frame.Width > 0 && frame.Height > 0)
                Graphics::DrawTexture(sheet,
                    (float)frame.X, (float)frame.Y, (float)frame.Width, (float)frame.Height,
                    x, y, side, side);
        }
    }
    else
        UIDraw::FillRect(x, y, side, side, UI_COL_FIELD);

    // Solid area.
    for (int column = 0; column < 16; column++) {
        Uint8 height = entry.Heights[column];
        if (height == TILE_COLLISION_NONE || height > 15)
            continue;

        float columnX = x + column * cell;

        if (entry.IsCeiling)
            UIDraw::FillRect(columnX, y, cell, (15 - height + 1) * cell, 0x804FA3FF);
        else
            UIDraw::FillRect(columnX, y + height * cell, cell, (16 - height) * cell, 0x804FA3FF);
    }

    if (ShowGrid) {
        for (int i = 0; i <= 16; i++) {
            Uint32 color = (i % 4) == 0 ? 0x50FFFFFF : 0x28FFFFFF;
            UIDraw::FillRect(x + i * cell, y, 1.0f, side, color);
            UIDraw::FillRect(x, y + i * cell, side, 1.0f, color);
        }
    }

    UIDraw::StrokeRect(x, y, side, side, UI_COL_BORDER_LIGHT);

    // Drawing. Pressing on a column sets its height to the row under the
    // pointer; pressing on a column that already reaches that far clears it,
    // which is how a surface gets rubbed out without a separate tool.
    bool over = UICore::IsOver(x, y, side, side);

    if (over && UICore::MouseWasPressed) {
        int column = (int)((UICore::MouseX - x) / cell);
        int row = (int)((UICore::MouseY - y) / cell);

        if (column >= 0 && column < 16 && row >= 0 && row < 16) {
            Uint8 existing = entry.Heights[column];
            Uint8 wanted = (Uint8)(entry.IsCeiling ? 15 - row : row);

            Painting = true;
            PaintingClears = existing == wanted;

            // A press and its release can both land inside one frame, so the
            // column under the pointer is painted here rather than left to the
            // drag below, which would never see the button still held.
            TileCollisionEditor::PaintColumn(&entry, column, row);
        }
    }
    else if (Painting && UICore::MouseIsDown && over) {
        int column = (int)((UICore::MouseX - x) / cell);
        int row = (int)((UICore::MouseY - y) / cell);

        if (column >= 0 && column < 16 && row >= 0 && row < 16)
            TileCollisionEditor::PaintColumn(&entry, column, row);
    }

    if (UICore::MouseWasReleased)
        Painting = false;
}

// The tile picker, so a tile can be chosen without leaving the panel.
PRIVATE STATIC void TileCollisionEditor::DrawTilePicker(float width) {
    size_t tileCount = Scene::TileSpriteInfos.size();

    float cell = 18.0f * UIDraw::Scale;
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

        // The marks and highlights below leave their colour in the shared blend
        // state, so it has to be put back before each tile is drawn.
        Graphics::SetBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

        TileSpriteInfo& info = Scene::TileSpriteInfos[i];
        if (info.Sprite && !Graphics::SpriteRangeCheck(info.Sprite, info.AnimationIndex, info.FrameIndex)) {
            AnimFrame& frame = info.Sprite->Animations[info.AnimationIndex].Frames[info.FrameIndex];
            Texture* sheet = info.Sprite->Spritesheets[frame.SheetNumber];

            if (sheet && frame.Width > 0 && frame.Height > 0)
                Graphics::DrawTexture(sheet,
                    (float)frame.X, (float)frame.Y, (float)frame.Width, (float)frame.Height,
                    cellX, cellY, cell, cell);
        }

        // A corner mark on tiles that already have collision, so the ones still
        // needing work stand out.
        if (i < Planes[CurrentPlane].size() && Planes[CurrentPlane][i].HasCollision)
            UIDraw::FillRect(cellX, cellY, 3.0f * UIDraw::Scale, 3.0f * UIDraw::Scale, UI_COL_SUCCESS);

        if (UICore::IsOver(cellX, cellY, cell, cell)) {
            UIDraw::StrokeRect(cellX, cellY, cell, cell, UI_COL_TEXT);

            if (UICore::MouseWasPressed)
                SelectedTile = (int)i;
        }

        if ((int)i == SelectedTile)
            UIDraw::StrokeRect(cellX, cellY, cell, cell, UI_COL_ACCENT);
    }
}

PUBLIC STATIC void TileCollisionEditor::Draw(float x, float y, float width, float height, bool split) {
    if (!Loaded)
        TileCollisionEditor::Reload();

    float halfW = split ? width / 2.0f : width;
    float halfH = split ? height : height / 2.0f;
    float secondX = split ? x + halfW : x;
    float secondY = split ? y : y + halfH;

    UICore::BeginPanel("Tile Collision", x, y, halfW, halfH);
        if (!TileCollisionEditor::IsAvailable()) {
            UICore::Text("This scene has no tile collision loaded.", UI_COL_TEXT_DIM);
            UICore::Text("Scenes get theirs from a TileConfig.bin", UI_COL_TEXT_FAINT);
            UICore::Text("next to the scene file.", UI_COL_TEXT_FAINT);
        }
        else {
            if (SelectedTile >= (int)Planes[CurrentPlane].size())
                SelectedTile = 0;

            TileCollisionEntry& entry = Planes[CurrentPlane][SelectedTile];

            static const char* const PlaneNames[] = { "Plane A", "Plane B" };
            UICore::Dropdown("Plane", PlaneNames, 2, &CurrentPlane);
            UICore::Tooltip("Hatch scenes carry two independent collision planes.");

            UICore::FieldFormatted("Tile", "%d", SelectedTile);

            TileCollisionEditor::DrawTileCanvas(UICore::ContentWidth());

            UICore::Text("Click a square to set that column's surface.", UI_COL_TEXT_FAINT);
            UICore::Text("Click it again to clear the column.", UI_COL_TEXT_FAINT);
        }
    UICore::EndPanel();

    UICore::BeginPanel("Collision Properties", secondX, secondY, halfW, halfH);
        if (!TileCollisionEditor::IsAvailable()) {
            UICore::Text("Nothing to edit.", UI_COL_TEXT_FAINT);
        }
        else {
            TileCollisionEntry& entry = Planes[CurrentPlane][SelectedTile];

            bool isCeiling = entry.IsCeiling != 0;
            if (UICore::Checkbox("Ceiling tile", &isCeiling)) {
                entry.IsCeiling = isCeiling ? 1 : 0;
                TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
            }
            UICore::Tooltip("Solid grows up from the bottom instead of down from the surface.");

            int angle = entry.Angle;
            if (UICore::SliderInt("Angle", &angle, 0, 255)) {
                entry.Angle = (Uint8)angle;
                TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
            }

            // The engine keeps angles as a byte around the circle, which is
            // easier to judge in degrees.
            UICore::FieldFormatted("In degrees", "%d", (int)(entry.Angle * 360 / 256));
            UICore::Field("Has collision", entry.HasCollision ? "yes" : "no");

            UICore::Separator();

            float buttonWidth = UICore::ContentWidth() / 3.0f - UICore::Pad();

            UICore::SetNextItemWidth(buttonWidth);
            if (UICore::Button("Fill")) {
                for (int i = 0; i < 16; i++)
                    entry.Heights[i] = 0;
                entry.HasCollision = true;
                TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
            }

            UICore::SameLine();
            UICore::SetNextItemWidth(buttonWidth);
            if (UICore::Button("Clear")) {
                for (int i = 0; i < 16; i++)
                    entry.Heights[i] = TILE_COLLISION_NONE;
                entry.HasCollision = false;
                entry.Angle = 0;
                TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
            }

            UICore::SameLine();
            UICore::SetNextItemWidth(buttonWidth);
            if (UICore::Button("Half")) {
                for (int i = 0; i < 16; i++)
                    entry.Heights[i] = 8;
                entry.HasCollision = true;
                TileCollisionEditor::ApplyToScene(CurrentPlane, SelectedTile);
            }

            int otherPlane = CurrentPlane == 0 ? 1 : 0;
            char copyLabel[64];
            snprintf(copyLabel, sizeof(copyLabel), "Copy To Plane %c", otherPlane == 0 ? 'A' : 'B');

            if (UICore::ButtonEnabled(copyLabel, Planes[otherPlane].size() > 0)) {
                Planes[otherPlane][SelectedTile] = entry;
                TileCollisionEditor::ApplyToScene(otherPlane, SelectedTile);
                TileCollisionEditor::SetStatus("Copied tile %d to the other plane.", SelectedTile);
            }

            UICore::Separator();

            UICore::Checkbox("Show the tile underneath", &ShowTileImage);
            UICore::Checkbox("Show grid", &ShowGrid);

            UICore::Separator();

            if (UICore::ButtonEnabled("Save Tile Collision", TileCollisionEditor::CanSave()))
                TileCollisionEditor::Save();
            UICore::Tooltip("Writes every tile's collision back to the scene's TileConfig.bin.");

            if (UICore::Button("Reload From Scene"))
                TileCollisionEditor::Invalidate();

            UICore::Field("State", TileCollisionEditor::UnsavedChanges ? "edited, not saved" : "saved");

            UICore::Separator();
            UICore::Heading("Pick A Tile");
            TileCollisionEditor::DrawTilePicker(UICore::ContentWidth());
        }
    UICore::EndPanel();
}
