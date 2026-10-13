#ifndef ENGINE_UI_SCENEEDITOR3D_H
#define ENGINE_UI_SCENEEDITOR3D_H

class Scene3DObject;

#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/Scene3DTypes.h>

class SceneEditor3D {
private:
    static void SetStatus(const char* format, ...);
    static bool HasExtension(const char* path, const char* extension);
    static bool IsModelFile(const char* path);
    static void ScanForModels();
    static void ScanForShaders();
    static void ScanForScenes();
    static bool LoadModel(const char* path);
    static void UnloadScene();
    static void LoadObjectModel(Scene3DObject* object);
    static bool OpenScene(const char* path);
    static bool SaveScene();
    static bool CreateScene(const char* name);
    static void DrawPreview(float x, float y, float w, float h);
    static void DrawColor(const char* label, float* color);
    static void DrawModelPanel(float x, float y, float w, float h);
    static void DrawMaterialPanel(float x, float y, float w, float h);
    static void DrawShaderPanel(float x, float y, float w, float h);
    static void DrawSceneEditPanel(float x, float y, float w, float h);

public:
    static bool Loaded;

    static const char* GetStatus();
    static void Unload();
    static void Draw(float x, float y, float w, float h, bool split);
};

#endif /* ENGINE_UI_SCENEEDITOR3D_H */
