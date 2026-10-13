#ifndef ENGINE_UI_INSPECTOR_H
#define ENGINE_UI_INSPECTOR_H

class Entity;
class SceneLayer;

#include <Engine/Includes/Standard.h>

class Inspector {
private:
    static void DrawEntity(Entity* entity);
    static void DrawLayer(SceneLayer* layer, int index);

public:
    static void Draw();
};

#endif /* ENGINE_UI_INSPECTOR_H */
