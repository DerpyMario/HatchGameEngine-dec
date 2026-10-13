#ifndef ENGINE_UI_SELECTION_H
#define ENGINE_UI_SELECTION_H

class Entity;
class SceneLayer;

#include <Engine/Includes/Standard.h>

class Selection {
public:
    enum {
        KIND_NONE = 0,
        KIND_ENTITY, 
        KIND_LAYER 
    }; 

    static void Validate();
    static void Clear();
    static void SetEntity(Entity* entity);
    static void SetLayer(int index);
    static int GetKind();
    static Entity* GetEntity();
    static int GetLayerIndex();
    static SceneLayer* GetLayer();
    static Uint32 GetVersion();
    static void GetName(char* out, size_t outSize);
};

#endif /* ENGINE_UI_SELECTION_H */
