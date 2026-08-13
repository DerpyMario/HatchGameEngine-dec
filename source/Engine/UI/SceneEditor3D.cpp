#if INTERFACE
#include <Engine/Includes/Standard.h>

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
#include <Engine/Rendering/Material.h>
#include <Engine/Rendering/Mesh.h>
#include <Engine/Rendering/Shader.h>
#include <Engine/ResourceTypes/IModel.h>
#include <Engine/ResourceTypes/ResourceManager.h>
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

PRIVATE STATIC void SceneEditor3D::DrawScenePanel(float x, float y, float w, float h) {
    UICore::BeginPanel("3D Scene", x, y, w, h);
        // The scene's 3D state belongs to whichever 3D scene a script made, so
        // this shows what the engine is actually set up to draw with.
        bool any = false;

        for (Uint32 i = 0; i < MAX_3D_SCENES; i++) {
            Scene3D* scene = &Graphics::Scene3Ds[i];
            if (!scene->Initialized)
                continue;

            any = true;

            char heading[64];
            snprintf(heading, sizeof(heading), "Scene %d", (int)i);
            UICore::Heading(heading);

            UICore::FieldFormatted("Field of view", "%.1f", scene->FOV);
            UICore::FieldFormatted("Near plane", "%.2f", scene->NearClippingPlane);
            UICore::FieldFormatted("Far plane", "%.1f", scene->FarClippingPlane);
            UICore::FieldFormatted("Ambient light", "%.2f %.2f %.2f",
                scene->Lighting.Ambient.R, scene->Lighting.Ambient.G, scene->Lighting.Ambient.B);
            UICore::FieldFormatted("Diffuse light", "%.2f %.2f %.2f",
                scene->Lighting.Diffuse.R, scene->Lighting.Diffuse.G, scene->Lighting.Diffuse.B);
            UICore::FieldFormatted("Specular light", "%.2f %.2f %.2f",
                scene->Lighting.Specular.R, scene->Lighting.Specular.G, scene->Lighting.Specular.B);
            UICore::FieldFormatted("Fog", "%.2f to %.2f, density %.2f",
                scene->Fog.Start, scene->Fog.End, scene->Fog.Density);
            UICore::FieldFormatted("Point size", "%.1f", scene->PointSize);
            UICore::Field("Clips polygons", scene->ClipPolygons ? "yes" : "no");

            UICore::Separator();
        }

        if (!any) {
            UICore::Text("No 3D scene is set up.", UI_COL_TEXT_DIM);
            UICore::Text("A game makes one with Scene3D.Create, and its", UI_COL_TEXT_FAINT);
            UICore::Text("lighting, fog and camera show up here once it has.", UI_COL_TEXT_FAINT);
        }
    UICore::EndPanel();
}

PUBLIC STATIC void SceneEditor3D::Draw(float x, float y, float w, float h, bool split) {
    float halfW = split ? w / 2.0f : w;
    float halfH = split ? h / 2.0f : h / 4.0f;
    float secondX = split ? x + halfW : x;

    SceneEditor3D::DrawModelPanel(x, y, halfW, halfH);
    SceneEditor3D::DrawShaderPanel(x, y + halfH, halfW, h - halfH);
    SceneEditor3D::DrawMaterialPanel(secondX, y, halfW, halfH);
    SceneEditor3D::DrawScenePanel(secondX, y + halfH, halfW, h - halfH);
}
