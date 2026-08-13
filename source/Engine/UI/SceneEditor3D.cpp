#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/Scene3DTypes.h>

need_t Scene3DObject;

class SceneEditor3D {
public:
    static bool Loaded;
};
#endif

#include <Engine/UI/SceneEditor3D.h>
#include <Engine/UI/UICore.h>
#include <Engine/UI/UIDraw.h>
#include <Engine/UI/UITheme.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Graphics.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Math/Matrix4x4.h>
#include <Engine/Rendering/Material.h>
#include <Engine/Rendering/Mesh.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/Rendering/Shader.h>
#include <Engine/ResourceTypes/IModel.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/ResourceTypes/SceneFormats/Scene3DFormat.h>
#include <Engine/Scene.h>
#include <Engine/Utilities/StringUtils.h>

// The 3D side of the editor.
//
// The engine has had 3D in it all along -- models in three of its own formats
// plus whatever Open Asset Import can read, materials, lighting, fog, and a
// renderer for all of it -- but the only way to reach any of it was from a
// script, which meant a game had to be written before anything could be looked
// at. This puts it in front of you: what models a project has, whether they
// load, what is in them, what their materials say, and the shaders that draw
// them.

bool SceneEditor3D::Loaded = false;

// Models found in the project.
static vector<std::string> ModelFiles;
static bool  ModelsScanned = false;
static int   SelectedModel = -1;

static IModel* Model = NULL;
static std::string ModelPath;

static int   SelectedMaterial = -1;

// Shader sources found in the project, paired up by name.
static vector<std::string> ShaderNames;
static vector<std::string> ShaderVertexPaths;
static vector<std::string> ShaderFragmentPaths;
static bool  ShadersScanned = false;
static int   SelectedShader = -1;

static char  StatusText[512] = "";

// The 3D scene being put together, and where it came from.
static vector<Scene3DObject> Objects;
static Scene3DSettings Settings;
static std::string ScenePath;
static int   SelectedObject = -1;
static bool  SceneDirty = false;

// The engine's 3D scene the preview draws through. It is made once, the first
// time something is drawn, and reused.
static Sint32 PreviewScene = -1;

// Dragging in the preview turns the camera around what it is looking at.
static bool  Orbiting = false;
static float OrbitLastX = 0.0f;
static float OrbitLastY = 0.0f;

static char  NewSceneName[128] = "MyScene3D";
static vector<std::string> SceneFiles3D;
static bool  Scenes3DScanned = false;
static int   SelectedScene3D = -1;

PRIVATE STATIC void SceneEditor3D::SetStatus(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(StatusText, sizeof(StatusText), format, args);
    va_end(args);
}

PUBLIC STATIC const char* SceneEditor3D::GetStatus() {
    return StatusText;
}

// ------------------------------------------------------------- browsing ---

PRIVATE STATIC bool SceneEditor3D::HasExtension(const char* path, const char* extension) {
    size_t pathLength = strlen(path);
    size_t extLength = strlen(extension);
    if (pathLength < extLength)
        return false;

    return StringUtils::StrCaseStr(path + pathLength - extLength, extension) != NULL;
}

// Everything the engine can open as a model. The three of its own, and the
// common ones Open Asset Import handles when it is built in.
PRIVATE STATIC bool SceneEditor3D::IsModelFile(const char* path) {
    static const char* extensions[] = {
        ".hmdl", ".bin", ".md3", ".mdl",
        ".fbx", ".dae", ".gltf", ".glb", ".obj", ".blend", ".3ds", ".ply", ".stl"
    };

    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        if (SceneEditor3D::HasExtension(path, extensions[i]))
            return true;
    }

    return false;
}

PRIVATE STATIC void SceneEditor3D::ScanForModels() {
    ModelFiles.clear();

    if (ResourceManager::UsingDataFolder && Directory::Exists("Resources")) {
        vector<char*> files = Directory::GetFiles("Resources", "*.*", true);
        for (size_t i = 0; i < files.size(); i++) {
            const char* path = files[i];

            const char* relative = StringUtils::StrCaseStr(path, "Resources/");
            if (relative)
                relative += strlen("Resources/");
            else
                relative = path;

            // Scene tile data is .bin too, and it is not a model. Anything that
            // is not one simply fails to load, so this only keeps the obvious
            // ones out of the list.
            if (SceneEditor3D::IsModelFile(path) && !StringUtils::StrCaseStr(relative, "TileConfig"))
                ModelFiles.push_back(std::string(relative));

            free(files[i]);
        }
    }

    ModelsScanned = true;
}

// Shaders come in pairs. A vertex file and a fragment file that share a name
// are one shader, which is the layout every renderer that reads GLSL uses.
PRIVATE STATIC void SceneEditor3D::ScanForShaders() {
    ShaderNames.clear();
    ShaderVertexPaths.clear();
    ShaderFragmentPaths.clear();

    if (!ResourceManager::UsingDataFolder || !Directory::Exists("Resources")) {
        ShadersScanned = true;
        return;
    }

    vector<std::string> vertexFiles;
    vector<std::string> fragmentFiles;

    vector<char*> files = Directory::GetFiles("Resources", "*.*", true);
    for (size_t i = 0; i < files.size(); i++) {
        const char* path = files[i];

        const char* relative = StringUtils::StrCaseStr(path, "Resources/");
        if (relative)
            relative += strlen("Resources/");
        else
            relative = path;

        if (SceneEditor3D::HasExtension(relative, ".vert") || SceneEditor3D::HasExtension(relative, ".vs"))
            vertexFiles.push_back(std::string(relative));
        else if (SceneEditor3D::HasExtension(relative, ".frag") || SceneEditor3D::HasExtension(relative, ".fs"))
            fragmentFiles.push_back(std::string(relative));

        free(files[i]);
    }

    for (size_t i = 0; i < vertexFiles.size(); i++) {
        std::string base = vertexFiles[i];
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos)
            base = base.substr(0, dot);

        for (size_t j = 0; j < fragmentFiles.size(); j++) {
            std::string other = fragmentFiles[j];
            size_t otherDot = other.find_last_of('.');
            if (otherDot != std::string::npos)
                other = other.substr(0, otherDot);

            if (base == other) {
                ShaderNames.push_back(base);
                ShaderVertexPaths.push_back(vertexFiles[i]);
                ShaderFragmentPaths.push_back(fragmentFiles[j]);
                break;
            }
        }
    }

    ShadersScanned = true;
}

PRIVATE STATIC void SceneEditor3D::ScanForScenes() {
    SceneFiles3D.clear();

    if (ResourceManager::UsingDataFolder && Directory::Exists("Resources")) {
        vector<char*> files = Directory::GetFiles("Resources", "*.*", true);
        for (size_t i = 0; i < files.size(); i++) {
            const char* path = files[i];

            const char* relative = StringUtils::StrCaseStr(path, "Resources/");
            if (relative)
                relative += strlen("Resources/");
            else
                relative = path;

            if (SceneEditor3D::HasExtension(relative, ".scene3d"))
                SceneFiles3D.push_back(std::string(relative));

            free(files[i]);
        }
    }

    Scenes3DScanned = true;
}

// --------------------------------------------------------------- models ---

PUBLIC STATIC void SceneEditor3D::Unload() {
    delete Model;
    Model = NULL;

    ModelPath.clear();
    SelectedMaterial = -1;
    SceneEditor3D::Loaded = false;
}

PRIVATE STATIC bool SceneEditor3D::LoadModel(const char* path) {
    SceneEditor3D::Unload();

    ResourceStream* stream = ResourceStream::New(path);
    if (!stream) {
        SceneEditor3D::SetStatus("Could not open \"%s\".", path);
        return false;
    }

    IModel* model = new IModel();
    bool loaded = model->Load(stream, path);
    stream->Close();

    if (!loaded) {
        delete model;
        SceneEditor3D::SetStatus("\"%s\" is not a model the engine can read.", path);
        return false;
    }

    Model = model;
    ModelPath = path;
    SelectedMaterial = model->MaterialCount ? 0 : -1;
    SceneEditor3D::Loaded = true;

    SceneEditor3D::SetStatus("Loaded %s: %d mesh(es), %d vertices, %d material(s).",
        path, (int)model->MeshCount, (int)model->VertexCount, (int)model->MaterialCount);

    return true;
}

// ------------------------------------------------------------ 3D scenes ---

PRIVATE STATIC void SceneEditor3D::UnloadScene() {
    for (size_t i = 0; i < Objects.size(); i++)
        delete (IModel*)Objects[i].Model;

    Objects.clear();
    ScenePath.clear();
    SelectedObject = -1;
    SceneDirty = false;
    Settings = Scene3DSettings();
}

// A placed model is loaded when the scene is, and a model that will not load
// leaves its place in the scene rather than being dropped, so a scene with a
// missing file can still be saved without losing what was in it.
PRIVATE STATIC void SceneEditor3D::LoadObjectModel(Scene3DObject* object) {
    if (object->Model || object->Failed)
        return;

    ResourceStream* stream = ResourceStream::New(object->Source);
    if (!stream) {
        object->Failed = true;
        return;
    }

    IModel* model = new IModel();
    if (!model->Load(stream, object->Source)) {
        delete model;
        object->Failed = true;
    }
    else
        object->Model = model;

    stream->Close();
}

PRIVATE STATIC bool SceneEditor3D::OpenScene(const char* path) {
    SceneEditor3D::UnloadScene();

    if (!Scene3DFormat::Read(path, &Settings, &Objects)) {
        SceneEditor3D::SetStatus("Could not read \"%s\".", path);
        return false;
    }

    for (size_t i = 0; i < Objects.size(); i++)
        SceneEditor3D::LoadObjectModel(&Objects[i]);

    ScenePath = path;
    SelectedObject = Objects.size() ? 0 : -1;

    SceneEditor3D::SetStatus("Opened %s: %d model(s).", path, (int)Objects.size());

    return true;
}

PRIVATE STATIC bool SceneEditor3D::SaveScene() {
    if (ScenePath.empty()) {
        SceneEditor3D::SetStatus("This scene has nowhere to be saved to yet.");
        return false;
    }

    if (!Scene3DFormat::Write(ScenePath.c_str(), &Settings, &Objects)) {
        SceneEditor3D::SetStatus("Could not write \"%s\".", ScenePath.c_str());
        return false;
    }

    SceneDirty = false;
    SceneEditor3D::SetStatus("Saved %s.", ScenePath.c_str());

    return true;
}

PRIVATE STATIC bool SceneEditor3D::CreateScene(const char* name) {
    if (!ResourceManager::UsingDataFolder) {
        SceneEditor3D::SetStatus("3D scenes can only be created in a project folder.");
        return false;
    }

    if (!name || !*name) {
        SceneEditor3D::SetStatus("Enter a name for the new 3D scene.");
        return false;
    }

    for (const char* i = name; *i; i++) {
        if (*i == '/' || *i == '\\' || *i == ':') {
            SceneEditor3D::SetStatus("Scene name cannot contain path separators.");
            return false;
        }
    }

    if (!Directory::Exists("Resources/Scenes") && !Directory::CreatePath("Resources/Scenes")) {
        SceneEditor3D::SetStatus("Could not create Resources/Scenes.");
        return false;
    }

    char path[1024];
    snprintf(path, sizeof(path), "Resources/Scenes/%s.scene3d", name);

    SceneEditor3D::UnloadScene();

    if (!Scene3DFormat::Write(path, &Settings, &Objects)) {
        SceneEditor3D::SetStatus("Could not write \"%s\".", path);
        return false;
    }

    ScenePath = path;
    Scenes3DScanned = false;

    SceneEditor3D::SetStatus("Created %s. Add models to it and save.", path);

    return true;
}

// ------------------------------------------------------------- preview ---

// The preview draws through the engine's own 3D scene, which is what a game
// would draw through, so what shows here is what the renderer actually does
// with the models rather than a second opinion about them.
PRIVATE STATIC void SceneEditor3D::DrawPreview(float x, float y, float w, float h) {
    if (w < 8.0f || h < 8.0f)
        return;

    UIDraw::FillRect(x, y, w, h, 0xFF101418);

    if (!Objects.size()) {
        UIDraw::Text(x + 8.0f, y + 8.0f, "Add a model to see it here.", UI_COL_TEXT_FAINT);
        UIDraw::StrokeRect(x, y, w, h, UI_COL_BORDER);
        return;
    }

    if (PreviewScene < 0) {
        Uint32 made = Graphics::CreateScene3D(SCOPE_GAME);
        if (made == 0xFFFFFFFF) {
            UIDraw::Text(x + 8.0f, y + 8.0f, "No 3D scene could be made.", UI_COL_DANGER);
            UIDraw::StrokeRect(x, y, w, h, UI_COL_BORDER);
            return;
        }

        PreviewScene = (Sint32)made;
    }

    Scene3D* scene = &Graphics::Scene3Ds[PreviewScene];

    scene->FOV = Settings.FOV;
    scene->NearClippingPlane = Settings.NearClippingPlane;
    scene->FarClippingPlane = Settings.FarClippingPlane;
    scene->SetAmbientLighting((Uint32)(Settings.AmbientR * 0x100), (Uint32)(Settings.AmbientG * 0x100), (Uint32)(Settings.AmbientB * 0x100));
    scene->SetDiffuseLighting((Uint32)(Settings.DiffuseR * 0x100), (Uint32)(Settings.DiffuseG * 0x100), (Uint32)(Settings.DiffuseB * 0x100));
    scene->SetSpecularLighting((Uint32)(Settings.SpecularR * 0x100), (Uint32)(Settings.SpecularG * 0x100), (Uint32)(Settings.SpecularB * 0x100));

    // Where the camera sits, worked out from how far around and above the point
    // it is looking at the drag has taken it.
    float eyeX = Settings.TargetX + cosf(Settings.CameraPitch) * sinf(Settings.CameraYaw) * Settings.CameraDistance;
    float eyeY = Settings.TargetY + sinf(Settings.CameraPitch) * Settings.CameraDistance;
    float eyeZ = Settings.TargetZ + cosf(Settings.CameraPitch) * cosf(Settings.CameraYaw) * Settings.CameraDistance;

    Matrix4x4 view;
    Matrix4x4::LookAt(&view,
        eyeX, eyeY, eyeZ,
        Settings.TargetX, Settings.TargetY, Settings.TargetZ,
        0.0f, 1.0f, 0.0f);
    scene->SetViewMatrix(&view);

    Graphics::ClearScene3D(PreviewScene);
    Graphics::BindScene3D(PreviewScene);

    for (size_t i = 0; i < Objects.size(); i++) {
        Scene3DObject& object = Objects[i];
        if (!object.Model)
            continue;

        Matrix4x4 model, rotation, scale;
        Matrix4x4::IdentityRotationXYZ(&rotation, object.RotationX, object.RotationY, object.RotationZ);
        Matrix4x4::IdentityScale(&scale, object.ScaleX, object.ScaleY, object.ScaleZ);
        Matrix4x4::Multiply(&model, &rotation, &scale);
        Matrix4x4::Translate(&model, &model, object.X, object.Y, object.Z);

        Graphics::DrawModel(object.Model, 0, 0, &model, &rotation);
    }

    Graphics::DrawScene3D(PreviewScene, scene->DrawMode);

    UIDraw::StrokeRect(x, y, w, h, UI_COL_BORDER_LIGHT);

    // Dragging inside the preview turns the camera.
    bool over = UICore::IsOver(x, y, w, h);

    if (over && UICore::MouseWasPressed) {
        Orbiting = true;
        OrbitLastX = UICore::MouseX;
        OrbitLastY = UICore::MouseY;
    }

    if (Orbiting && UICore::MouseIsDown) {
        Settings.CameraYaw -= (UICore::MouseX - OrbitLastX) * 0.01f;
        Settings.CameraPitch += (UICore::MouseY - OrbitLastY) * 0.01f;

        // Stopping short of straight up and straight down keeps the camera from
        // turning over at the poles.
        if (Settings.CameraPitch > 1.5f)
            Settings.CameraPitch = 1.5f;
        if (Settings.CameraPitch < -1.5f)
            Settings.CameraPitch = -1.5f;

        OrbitLastX = UICore::MouseX;
        OrbitLastY = UICore::MouseY;
    }

    if (UICore::MouseWasReleased)
        Orbiting = false;

    if (over && UICore::MouseWheel != 0.0f) {
        Settings.CameraDistance *= UICore::MouseWheel > 0.0f ? 0.9f : 1.1f;
        if (Settings.CameraDistance < 1.0f)
            Settings.CameraDistance = 1.0f;
    }
}

// -------------------------------------------------------------- drawing ---

PRIVATE STATIC void SceneEditor3D::DrawColor(const char* label, float* color) {
    char name[128];

    for (int i = 0; i < 3; i++) {
        static const char* channels[] = { "red", "green", "blue" };
        snprintf(name, sizeof(name), "%s %s", label, channels[i]);

        int value = (int)(color[i] * 255.0f);
        if (UICore::SliderInt(name, &value, 0, 255))
            color[i] = value / 255.0f;
    }
}

PRIVATE STATIC void SceneEditor3D::DrawModelPanel(float x, float y, float w, float h) {
    UICore::BeginPanel("Models", x, y, w, h);
        if (!ResourceManager::UsingDataFolder)
            UICore::Text("Open a project folder to browse its models.", UI_COL_TEXT_FAINT);
        else {
            if (!ModelsScanned)
                SceneEditor3D::ScanForModels();

            if (!ModelFiles.size()) {
                UICore::Text("No models in this project.", UI_COL_TEXT_DIM);
                UICore::Text("The engine reads its own .hmdl, .md3 and RSDK", UI_COL_TEXT_FAINT);
                UICore::Text("models, and whatever Open Asset Import handles", UI_COL_TEXT_FAINT);
                UICore::Text("when it is built in.", UI_COL_TEXT_FAINT);
            }

            UICore::ResetRowStriping();
            for (size_t i = 0; i < ModelFiles.size(); i++) {
                char label[4200];
                snprintf(label, sizeof(label), "%s##model%d", ModelFiles[i].c_str(), (int)i);

                if (UICore::ListItem(label, SelectedModel == (int)i))
                    SelectedModel = (int)i;
            }

            UICore::Separator();

            bool haveSelection = SelectedModel >= 0 && SelectedModel < (int)ModelFiles.size();
            if (UICore::ButtonEnabled("Load Model", haveSelection))
                SceneEditor3D::LoadModel(ModelFiles[SelectedModel].c_str());

            if (UICore::Button("Rescan"))
                ModelsScanned = false;

            UICore::Separator();
            UICore::Heading("Loaded Model");

            if (!Model)
                UICore::Text("Nothing loaded.", UI_COL_TEXT_FAINT);
            else {
                UICore::Field("File", ModelPath.c_str());
                UICore::FieldFormatted("Meshes", "%d", (int)Model->MeshCount);
                UICore::FieldFormatted("Vertices", "%d", (int)Model->VertexCount);
                UICore::FieldFormatted("Vertices per face", "%d", (int)Model->VertexPerFace);
                UICore::FieldFormatted("Materials", "%d", (int)Model->MaterialCount);
                UICore::FieldFormatted("Animations", "%d", (int)Model->AnimationCount);
                UICore::FieldFormatted("Armatures", "%d", (int)Model->ArmatureCount);
                UICore::Field("Animated by", Model->UseVertexAnimation ? "vertices" : "bones");

                for (size_t i = 0; i < Model->MeshCount && i < 16; i++) {
                    Mesh* mesh = Model->Meshes[i];
                    if (!mesh)
                        continue;

                    char label[256];
                    snprintf(label, sizeof(label), "Mesh %d", (int)i);
                    UICore::FieldFormatted(label, "%s, %d vertices",
                        mesh->Name ? mesh->Name : "unnamed", (int)mesh->VertexCount);
                }

                if (UICore::Button("Unload"))
                    SceneEditor3D::Unload();
            }
        }
    UICore::EndPanel();
}

PRIVATE STATIC void SceneEditor3D::DrawMaterialPanel(float x, float y, float w, float h) {
    UICore::BeginPanel("Materials", x, y, w, h);
        if (!Model)
            UICore::Text("Load a model to see its materials.", UI_COL_TEXT_FAINT);
        else if (!Model->MaterialCount)
            UICore::Text("This model has no materials.", UI_COL_TEXT_DIM);
        else {
            UICore::ResetRowStriping();
            for (size_t i = 0; i < Model->MaterialCount; i++) {
                Material* material = Model->Materials[i];

                char label[512];
                snprintf(label, sizeof(label), "%s##material%d",
                    material && material->Name ? material->Name : "Unnamed", (int)i);

                if (UICore::ListItem(label, SelectedMaterial == (int)i))
                    SelectedMaterial = (int)i;
            }

            UICore::Separator();

            if (SelectedMaterial >= 0 && SelectedMaterial < (int)Model->MaterialCount &&
                Model->Materials[SelectedMaterial]) {
                Material* material = Model->Materials[SelectedMaterial];

                UICore::Heading("Colours");
                SceneEditor3D::DrawColor("Diffuse", material->ColorDiffuse);
                SceneEditor3D::DrawColor("Specular", material->ColorSpecular);
                SceneEditor3D::DrawColor("Ambient", material->ColorAmbient);
                SceneEditor3D::DrawColor("Emissive", material->ColorEmissive);

                UICore::Separator();
                UICore::Heading("Surface");

                int shininess = (int)material->Shininess;
                if (UICore::SliderInt("Shininess", &shininess, 0, 128))
                    material->Shininess = (float)shininess;

                int strength = (int)(material->ShininessStrength * 100.0f);
                if (UICore::SliderInt("Shininess strength", &strength, 0, 100))
                    material->ShininessStrength = strength / 100.0f;

                int opacity = (int)(material->Opacity * 100.0f);
                if (UICore::SliderInt("Opacity", &opacity, 0, 100))
                    material->Opacity = opacity / 100.0f;

                UICore::Separator();
                UICore::Heading("Textures");
                UICore::Field("Diffuse", material->TextureDiffuseName ? material->TextureDiffuseName : "-");
                UICore::Field("Specular", material->TextureSpecularName ? material->TextureSpecularName : "-");
                UICore::Field("Ambient", material->TextureAmbientName ? material->TextureAmbientName : "-");
                UICore::Field("Emissive", material->TextureEmissiveName ? material->TextureEmissiveName : "-");
            }
        }
    UICore::EndPanel();
}

PRIVATE STATIC void SceneEditor3D::DrawShaderPanel(float x, float y, float w, float h) {
    UICore::BeginPanel("Shaders", x, y, w, h);
        if (!ResourceManager::UsingDataFolder)
            UICore::Text("Open a project folder to browse its shaders.", UI_COL_TEXT_FAINT);
        else {
            if (!ShadersScanned)
                SceneEditor3D::ScanForShaders();

            if (!ShaderNames.size()) {
                UICore::Text("No shaders in this project.", UI_COL_TEXT_DIM);
                UICore::Text("A shader is a .vert and a .frag sharing a name,", UI_COL_TEXT_FAINT);
                UICore::Text("anywhere under Resources.", UI_COL_TEXT_FAINT);
            }

            UICore::ResetRowStriping();
            for (size_t i = 0; i < ShaderNames.size(); i++) {
                char label[4200];
                snprintf(label, sizeof(label), "%s##shader%d", ShaderNames[i].c_str(), (int)i);

                if (UICore::ListItem(label, SelectedShader == (int)i))
                    SelectedShader = (int)i;
            }

            UICore::Separator();

            bool haveSelection = SelectedShader >= 0 && SelectedShader < (int)ShaderNames.size();

            if (UICore::ButtonEnabled("Build Shader", haveSelection)) {
                int index = Shader::LoadInto(ShaderVertexPaths[SelectedShader].c_str(),
                    ShaderFragmentPaths[SelectedShader].c_str());

                Shader* built = Shader::Get(index);

                if (index < 0 || !built || built->Failed)
                    SceneEditor3D::SetStatus("\"%s\" did not build; the console says why.",
                        ShaderNames[SelectedShader].c_str());
                else
                    SceneEditor3D::SetStatus("Built \"%s\" as shader %d.",
                        ShaderNames[SelectedShader].c_str(), index);
            }

            if (UICore::Button("Rescan Shaders"))
                ShadersScanned = false;

            UICore::Separator();
            UICore::Heading("Built");

            if (!Shader::GetCount())
                UICore::Text("Nothing built yet.", UI_COL_TEXT_FAINT);

            for (int i = 0; i < Shader::GetCount(); i++) {
                Shader* shader = Shader::Get(i);
                if (!shader)
                    continue;

                char label[512];
                snprintf(label, sizeof(label), "%d: %s", i,
                    shader->Name ? shader->Name : "unnamed");

                UICore::Field(label, shader->Failed ? "failed" : "ready");

                char button[512];
                snprintf(button, sizeof(button), "Rebuild##rebuild%d", i);
                if (UICore::Button(button)) {
                    if (shader->Reload())
                        SceneEditor3D::SetStatus("Built shader %d again.", i);
                    else
                        SceneEditor3D::SetStatus("Shader %d did not build; the one loaded is still in use.", i);
                }
            }
        }
    UICore::EndPanel();
}

PRIVATE STATIC void SceneEditor3D::DrawSceneEditPanel(float x, float y, float w, float h) {
    UICore::BeginPanel("3D Scene", x, y, w, h);
        // The preview takes the top of the panel, and what it is showing is
        // edited underneath it.
        float previewH = h * 0.45f;
        float previewX, previewY;
        UICore::PlaceCustomItem(w - UICore::Pad() * 4.0f, previewH, &previewX, &previewY);
        SceneEditor3D::DrawPreview(previewX, previewY, w - UICore::Pad() * 4.0f, previewH);

        UICore::Text("Drag to turn the camera; the wheel moves it closer.", UI_COL_TEXT_FAINT);

        UICore::Field("Scene", ScenePath.empty() ? "(unsaved)" : ScenePath.c_str());

        if (!ResourceManager::UsingDataFolder)
            UICore::Text("Open a project folder to keep 3D scenes in it.", UI_COL_TEXT_FAINT);
        else {
            UICore::TextField("New scene name", NewSceneName, sizeof(NewSceneName));

            if (UICore::Button("Create 3D Scene"))
                SceneEditor3D::CreateScene(NewSceneName);

            if (UICore::ButtonEnabled("Save 3D Scene", !ScenePath.empty()))
                SceneEditor3D::SaveScene();

            if (!Scenes3DScanned)
                SceneEditor3D::ScanForScenes();

            UICore::ResetRowStriping();
            for (size_t i = 0; i < SceneFiles3D.size(); i++) {
                char label[4200];
                snprintf(label, sizeof(label), "%s##scene3d%d", SceneFiles3D[i].c_str(), (int)i);

                if (UICore::ListItem(label, SelectedScene3D == (int)i))
                    SelectedScene3D = (int)i;
            }

            bool haveScene = SelectedScene3D >= 0 && SelectedScene3D < (int)SceneFiles3D.size();
            if (UICore::ButtonEnabled("Open 3D Scene", haveScene))
                SceneEditor3D::OpenScene(SceneFiles3D[SelectedScene3D].c_str());

            if (UICore::Button("Rescan 3D Scenes"))
                Scenes3DScanned = false;
        }

        UICore::Separator();
        UICore::Heading("Models In This Scene");

        bool haveModel = SelectedModel >= 0 && SelectedModel < (int)ModelFiles.size();
        if (UICore::ButtonEnabled("Add The Selected Model", haveModel)) {
            Scene3DObject object;
            snprintf(object.Source, sizeof(object.Source), "%s", ModelFiles[SelectedModel].c_str());

            SceneEditor3D::LoadObjectModel(&object);

            Objects.push_back(object);
            SelectedObject = (int)Objects.size() - 1;
            SceneDirty = true;

            if (object.Failed)
                SceneEditor3D::SetStatus("Added \"%s\", which did not load.", object.Source);
            else
                SceneEditor3D::SetStatus("Added \"%s\" to the scene.", object.Source);
        }

        if (!Objects.size())
            UICore::Text("Nothing in this scene yet.", UI_COL_TEXT_FAINT);

        UICore::ResetRowStriping();
        for (size_t i = 0; i < Objects.size(); i++) {
            char label[600];
            snprintf(label, sizeof(label), "%s%s##object%d",
                Objects[i].Source, Objects[i].Failed ? "  (did not load)" : "", (int)i);

            if (UICore::ListItem(label, SelectedObject == (int)i))
                SelectedObject = (int)i;
        }

        if (SelectedObject >= 0 && SelectedObject < (int)Objects.size()) {
            Scene3DObject& object = Objects[SelectedObject];

            UICore::Separator();
            UICore::Heading("Placement");

            int values[9] = {
                (int)object.X, (int)object.Y, (int)object.Z,
                (int)(object.RotationX * 100.0f), (int)(object.RotationY * 100.0f), (int)(object.RotationZ * 100.0f),
                (int)(object.ScaleX * 100.0f), (int)(object.ScaleY * 100.0f), (int)(object.ScaleZ * 100.0f)
            };

            static const char* names[9] = {
                "X", "Y", "Z",
                "Turn about X", "Turn about Y", "Turn about Z",
                "Scale X", "Scale Y", "Scale Z"
            };

            bool changed = false;
            for (int i = 0; i < 9; i++) {
                int minimum = i < 3 ? -4096 : (i < 6 ? -628 : 1);
                int maximum = i < 3 ? 4096 : (i < 6 ? 628 : 1000);

                if (UICore::SliderInt(names[i], &values[i], minimum, maximum))
                    changed = true;
            }

            if (changed) {
                object.X = (float)values[0];
                object.Y = (float)values[1];
                object.Z = (float)values[2];
                object.RotationX = values[3] / 100.0f;
                object.RotationY = values[4] / 100.0f;
                object.RotationZ = values[5] / 100.0f;
                object.ScaleX = values[6] / 100.0f;
                object.ScaleY = values[7] / 100.0f;
                object.ScaleZ = values[8] / 100.0f;
                SceneDirty = true;
            }

            if (UICore::Button("Remove From Scene")) {
                delete (IModel*)Objects[SelectedObject].Model;
                Objects.erase(Objects.begin() + SelectedObject);
                SelectedObject = Objects.size() ? 0 : -1;
                SceneDirty = true;
            }
        }

        UICore::Separator();
        UICore::Heading("Camera And Light");

        int fov = (int)Settings.FOV;
        if (UICore::SliderInt("Field of view", &fov, 10, 170)) {
            Settings.FOV = (float)fov;
            SceneDirty = true;
        }

        SceneEditor3D::DrawColor("Ambient", &Settings.AmbientR);
        SceneEditor3D::DrawColor("Diffuse", &Settings.DiffuseR);

        if (SceneDirty)
            UICore::Text("This scene has changes that are not saved.", UI_COL_WARNING);
    UICore::EndPanel();
}

PUBLIC STATIC void SceneEditor3D::Draw(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h / 2.0f : h / 4.0f;
    float secondX = split ? x + halfW : x;

    SceneEditor3D::DrawModelPanel(x, y, halfW, halfH);
    SceneEditor3D::DrawShaderPanel(x, y + halfH, halfW, (h - halfH) / 2.0f);
    SceneEditor3D::DrawMaterialPanel(x, y + halfH + (h - halfH) / 2.0f, halfW, (h - halfH) / 2.0f);
    SceneEditor3D::DrawSceneEditPanel(secondX, y, halfW, h);
}
