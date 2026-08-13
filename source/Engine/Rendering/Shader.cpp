#if INTERFACE
#include <Engine/Includes/Standard.h>
class Shader {
public:
    char* Name = nullptr;
    char* VertexPath = nullptr;
    char* FragmentPath = nullptr;

    void* Handle = nullptr;
    bool  Failed = false;
};
#endif

#include <Engine/Rendering/Shader.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Graphics.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Utilities/StringUtils.h>

// A shader the game wrote, rather than one the engine generated for itself.
//
// The engine builds its own shaders as it needs them, which leaves no way to
// use one of your own. This loads a pair of GLSL files out of the game's
// resources and hands them to the renderer to build.
//
// A shader written for this talks to the engine through the names its own
// shaders use, since those are what the draw path fills in:
//
//   attributes  i_position, i_uv, i_color
//   uniforms    u_projectionMatrix, u_modelViewMatrix, u_color, u_texture,
//               u_paletteTexture, u_fogColor, u_fogLinearStart,
//               u_fogLinearEnd, u_fogDensity, u_fogTable
//
// Anything the shader does not declare is simply not set, so a shader only has
// to name the parts it uses.

PRIVATE STATIC char* Shader::ReadSource(const char* path) {
    ResourceStream* stream = ResourceStream::New(path);
    if (!stream) {
        Log::Print(Log::LOG_ERROR, "Could not open the shader source \"%s\"!", path);
        return NULL;
    }

    size_t length = stream->Length();
    char* source = (char*)Memory::Malloc(length + 1);
    if (!source) {
        stream->Close();
        return NULL;
    }

    stream->ReadBytes(source, length);
    source[length] = '\0';
    stream->Close();

    return source;
}

PUBLIC STATIC Shader* Shader::Load(const char* vertexPath, const char* fragmentPath) {
    if (!vertexPath || !fragmentPath)
        return NULL;

    char* vertexSource = Shader::ReadSource(vertexPath);
    if (!vertexSource)
        return NULL;

    char* fragmentSource = Shader::ReadSource(fragmentPath);
    if (!fragmentSource) {
        Memory::Free(vertexSource);
        return NULL;
    }

    Shader* shader = new Shader();
    shader->VertexPath = StringUtils::Duplicate(vertexPath);
    shader->FragmentPath = StringUtils::Duplicate(fragmentPath);
    shader->Name = StringUtils::Duplicate(vertexPath);

    shader->Handle = Graphics::CreateShader(vertexSource, fragmentSource);
    shader->Failed = shader->Handle == NULL;

    Memory::Free(vertexSource);
    Memory::Free(fragmentSource);

    if (shader->Failed)
        Log::Print(Log::LOG_ERROR, "Could not build the shader from \"%s\" and \"%s\".", vertexPath, fragmentPath);
    else
        Log::Print(Log::LOG_VERBOSE, "Built the shader from \"%s\" and \"%s\".", vertexPath, fragmentPath);

    return shader;
}

// Builds the shader again from the same two files, so an edit can be seen
// without restarting. The old one is only let go once the new one is built, so
// a shader that stops compiling leaves the working one in place.
PUBLIC bool Shader::Reload() {
    if (!VertexPath || !FragmentPath)
        return false;

    char* vertexSource = Shader::ReadSource(VertexPath);
    if (!vertexSource)
        return false;

    char* fragmentSource = Shader::ReadSource(FragmentPath);
    if (!fragmentSource) {
        Memory::Free(vertexSource);
        return false;
    }

    void* rebuilt = Graphics::CreateShader(vertexSource, fragmentSource);

    Memory::Free(vertexSource);
    Memory::Free(fragmentSource);

    if (!rebuilt) {
        Log::Print(Log::LOG_ERROR, "Could not build \"%s\" again; the one already loaded is still in use.", VertexPath);
        return false;
    }

    Graphics::DeleteShader(Handle);

    Handle = rebuilt;
    Failed = false;

    return true;
}

PUBLIC void Shader::Use() {
    if (Handle)
        Graphics::UseShader(Handle);
}

PUBLIC STATIC void Shader::Unuse() {
    Graphics::UseShader(NULL);
}

PUBLIC void Shader::Dispose() {
    Graphics::DeleteShader(Handle);
    Handle = NULL;

    Memory::Free(Name);
    Memory::Free(VertexPath);
    Memory::Free(FragmentPath);

    Name = NULL;
    VertexPath = NULL;
    FragmentPath = NULL;
}

PUBLIC Shader::~Shader() {
    Dispose();
}

// ------------------------------------------------------------- the loaded ---

// Every shader the game has loaded, so scripts can name one by number and the
// editor can list them without keeping its own copy of the same thing.
static vector<Shader*> LoadedShaders;

PUBLIC STATIC int Shader::LoadInto(const char* vertexPath, const char* fragmentPath) {
    // A shader already loaded from the same pair of files is handed back rather
    // than built a second time.
    for (size_t i = 0; i < LoadedShaders.size(); i++) {
        Shader* other = LoadedShaders[i];
        if (other && other->VertexPath && other->FragmentPath &&
            !strcmp(other->VertexPath, vertexPath) &&
            !strcmp(other->FragmentPath, fragmentPath))
            return (int)i;
    }

    Shader* shader = Shader::Load(vertexPath, fragmentPath);
    if (!shader)
        return -1;

    LoadedShaders.push_back(shader);

    return (int)LoadedShaders.size() - 1;
}

PUBLIC STATIC int Shader::GetCount() {
    return (int)LoadedShaders.size();
}

PUBLIC STATIC Shader* Shader::Get(int index) {
    if (index < 0 || index >= (int)LoadedShaders.size())
        return NULL;

    return LoadedShaders[index];
}

PUBLIC STATIC void Shader::UnloadAll() {
    Shader::Unuse();

    for (size_t i = 0; i < LoadedShaders.size(); i++)
        delete LoadedShaders[i];

    LoadedShaders.clear();
}
