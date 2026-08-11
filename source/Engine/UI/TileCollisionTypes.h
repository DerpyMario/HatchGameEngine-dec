#ifndef ENGINE_UI_TILECOLLISIONTYPES_H
#define ENGINE_UI_TILECOLLISIONTYPES_H

// What a collision file holds for one tile, and what the collision editor
// works on. Everything else the engine collides against is derived from these
// column heights.
struct TileCollisionEntry {
    Uint8 Heights[16];
    bool  HasCollision = false;
    Uint8 IsCeiling = 0;
    Uint8 Angle = 0;
};

#endif /* ENGINE_UI_TILECOLLISIONTYPES_H */
