#ifndef ENGINE_SCENE_SCENECONFIG_H
#define ENGINE_SCENE_SCENECONFIG_H

#include <Engine/Includes/HashMap.h>

struct SceneListCategory {
    char*           Name = nullptr;

    size_t          OffsetStart = 0;
    size_t          OffsetEnd = 0;
    size_t          Count = 0;

    HashMap<char*>* Properties = nullptr;
};

struct SceneListEntry {
    char*           Name = nullptr;
    char*           Folder = nullptr;
    char*           ID = nullptr;
    char*           SpriteFolder = nullptr;
    char*           Filetype = nullptr;

    size_t          ParentCategoryID = 0;
    size_t          CategoryPos = 0;

    HashMap<char*>* Properties = nullptr;
};

#endif /* ENGINE_SCENE_SCENECONFIG_H */
