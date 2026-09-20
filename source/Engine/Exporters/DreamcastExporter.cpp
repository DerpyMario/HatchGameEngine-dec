#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/DreamcastTypes.h>
#include <Engine/Rendering/Scene3DTypes.h>

need_t SceneLayer;
need_t Scene3DObject;
need_t Scene3DSettings;
need_t Mesh;
need_t IModel;

class DreamcastExporter {
public:

};
#endif

#include <Engine/Exporters/DreamcastExporter.h>
#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Math/Matrix4x4.h>
#include <Engine/Rendering/Material.h>
#include <Engine/Rendering/Mesh.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/ResourceTypes/IModel.h>
#include <Engine/ResourceTypes/SceneFormats/Scene3DFormat.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a SEGA Dreamcast can show.
//
// This is the one SEGA target where the machine is not the constraint. The
// others needed the art reduced to fit -- sixteen colours, three bits a
// channel, tiles deduplicated against a VRAM measured in kilobytes. Here the
// scene goes over in sixteen-bit colour and the awkward part is only that the
// PowerVR wants its textures square and a power of two, so a layer is cut up.
//
// Nothing here is bare metal either, unlike the Saturn and 32X exports.
// KallistiOS is an operating system with drivers for this hardware, and
// reimplementing it to avoid a dependency would be a worse export, not a purer
// one. What the runtime carries is the part above KOS: scene to polygons.

static vector<Uint16> Texels;      // RGB565, tile by tile, row major inside each
static int            TileColumns;
static int            TileRows;

static vector<DreamcastVertex> Vertices;
static vector<DreamcastFace>   Faces;
static int                     DroppedFaces;

// Five bits of red, six of green, five of blue -- the format the PVR reads a
// texture in, and the one that costs the least of the colour a scene was drawn
// with. Green gets the extra bit because the eye does.
PRIVATE STATIC Uint16 DreamcastExporter::ToRGB565(Uint32 argb) {
    Uint32 r = (argb >> 16) & 0xFF;
    Uint32 g = (argb >> 8) & 0xFF;
    Uint32 b = argb & 0xFF;

    return (Uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Cuts the layer into the square textures the PVR wants.
//
// A tile that hangs off the edge of the layer is still a whole texture; the
// part past the edge is left black rather than left uninitialised, so what runs
// off the end of a scene looks deliberate.
PRIVATE STATIC void DreamcastExporter::BuildTextures(SceneLayer* layer, int width, int height) {
    TileColumns = (width + DREAMCAST_TILE_SIZE - 1) / DREAMCAST_TILE_SIZE;
    TileRows = (height + DREAMCAST_TILE_SIZE - 1) / DREAMCAST_TILE_SIZE;

    size_t perTile = (size_t)DREAMCAST_TILE_SIZE * DREAMCAST_TILE_SIZE;

    Texels.assign(perTile * TileColumns * TileRows, 0);

    for (int row = 0; row < TileRows; row++) {
        for (int column = 0; column < TileColumns; column++) {
            Uint16* out = &Texels[perTile * (column + row * TileColumns)];

            for (int y = 0; y < DREAMCAST_TILE_SIZE; y++) {
                int sourceY = row * DREAMCAST_TILE_SIZE + y;

                for (int x = 0; x < DREAMCAST_TILE_SIZE; x++) {
                    int sourceX = column * DREAMCAST_TILE_SIZE + x;

                    if (sourceX >= width || sourceY >= height)
                        continue;

                    Uint32 pixel;
                    if (!SegaSceneArt::GetLayerPixel(layer, sourceX, sourceY, &pixel))
                        continue;

                    // A pixel the scene draws as transparent has no alpha to
                    // spend in RGB565, so it becomes black -- the same thing
                    // the tile exports do with index zero.
                    if (((pixel >> 24) & 0xFF) < 128)
                        continue;

                    out[x + y * DREAMCAST_TILE_SIZE] =
                        DreamcastExporter::ToRGB565(pixel & 0xFFFFFF);
                }
            }
        }
    }
}

PRIVATE STATIC bool DreamcastExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool DreamcastExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool DreamcastExporter::CopyFile(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");
    if (!in)
        return false;

    FILE* out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }

    char buffer[16384];
    size_t got;
    bool ok = true;

    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) {
            ok = false;
            break;
        }
    }

    fclose(in);
    fclose(out);

    return ok;
}

PRIVATE STATIC bool DreamcastExporter::FindRuntime(char* out, size_t outSize) {
    if (Application::DreamcastRuntimePath.size()) {
        StringUtils::Copy(out, Application::DreamcastRuntimePath.c_str(), outSize);
        return Directory::Exists(out);
    }

    const char* relative = "meta/dreamcast/runtime";

    if (Directory::Exists(relative)) {
        StringUtils::Copy(out, relative, outSize);
        return true;
    }

    char* base = SDL_GetBasePath();
    if (base) {
        snprintf(out, outSize, "%s%s", base, relative);
        SDL_free(base);

        if (Directory::Exists(out))
            return true;
    }

    return false;
}

// ------------------------------------------------------------ 3D scenes ---

// The colour a face is drawn in, as the PVR wants it: alpha in the top byte.
PRIVATE STATIC Uint32 DreamcastExporter::FaceColor(Mesh* mesh, IModel* model, Sint32* corners, int cornerCount) {
    if (mesh->ColorBuffer) {
        Uint32 r = 0, g = 0, b = 0;

        for (int i = 0; i < cornerCount; i++) {
            Uint32 c = mesh->ColorBuffer[corners[i]];
            r += (c >> 16) & 0xFF;
            g += (c >> 8) & 0xFF;
            b += c & 0xFF;
        }

        r /= (Uint32)cornerCount;
        g /= (Uint32)cornerCount;
        b /= (Uint32)cornerCount;

        return 0xFF000000U | (r << 16) | (g << 8) | b;
    }

    if (mesh->MaterialIndex >= 0 && model->Materials &&
        (size_t)mesh->MaterialIndex < model->MaterialCount) {
        Material* material = model->Materials[mesh->MaterialIndex];
        if (material) {
            Uint32 r = (Uint32)(material->ColorDiffuse[0] * 255.0f);
            Uint32 g = (Uint32)(material->ColorDiffuse[1] * 255.0f);
            Uint32 b = (Uint32)(material->ColorDiffuse[2] * 255.0f);

            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            if (r || g || b)
                return 0xFF000000U | (r << 16) | (g << 8) | b;
        }
    }

    return 0xFFA0A0A0U;
}

// Walks one placed model into the shared vertex and face tables, with its
// position, rotation and scale already applied.
PRIVATE STATIC void DreamcastExporter::CollectModel(IModel* model, Scene3DObject* object) {
    Matrix4x4 rotation, scale, transform;

    Matrix4x4::IdentityRotationXYZ(&rotation, object->RotationX, object->RotationY, object->RotationZ);
    Matrix4x4::IdentityScale(&scale, object->ScaleX, object->ScaleY, object->ScaleZ);
    Matrix4x4::Multiply(&transform, &rotation, &scale);
    Matrix4x4::Translate(&transform, &transform, object->X, object->Y, object->Z);

    for (size_t m = 0; m < model->MeshCount; m++) {
        Mesh* mesh = model->Meshes[m];
        if (!mesh || !mesh->PositionBuffer || !mesh->VertexIndexBuffer)
            continue;

        size_t base = Vertices.size();

        if (base + mesh->VertexCount > DREAMCAST_MAX_VERTICES)
            break;

        for (Uint32 v = 0; v < mesh->VertexCount; v++) {
            float x = (float)mesh->PositionBuffer[v].X / 65536.0f;
            float y = (float)mesh->PositionBuffer[v].Y / 65536.0f;
            float z = (float)mesh->PositionBuffer[v].Z / 65536.0f;

            // Matrix4x4 is column major, the way OpenGL lays one out: element
            // (row i, column j) is Values[j * 4 + i], and the translation is
            // the last column.
            float* M = transform.Values;

            DreamcastVertex vertex;
            vertex.X = M[0] * x + M[4] * y + M[8] * z + M[12];
            vertex.Y = M[1] * x + M[5] * y + M[9] * z + M[13];
            vertex.Z = M[2] * x + M[6] * y + M[10] * z + M[14];

            Vertices.push_back(vertex);
        }

        int perFace = model->VertexPerFace ? model->VertexPerFace : 3;

        for (Uint32 i = 0; i + perFace <= mesh->VertexIndexCount; i += perFace) {
            if (Faces.size() >= DREAMCAST_MAX_FACES) {
                DroppedFaces++;
                continue;
            }

            Sint32 corners[4];
            bool valid = true;

            for (int c = 0; c < perFace; c++) {
                Sint32 index = mesh->VertexIndexBuffer[i + c];
                if (index < 0 || (Uint32)index >= mesh->VertexCount) {
                    valid = false;
                    break;
                }
                corners[c] = index;
            }

            if (!valid)
                continue;

            DreamcastFace face;
            face.A = (Uint16)(base + corners[0]);
            face.B = (Uint16)(base + corners[1]);
            face.C = (Uint16)(base + corners[2]);

            if (perFace >= 4) {
                face.D = (Uint16)(base + corners[3]);
                face.Flags = 0;
            }
            else {
                face.D = face.C;
                face.Flags = DREAMCAST_FACE_TRIANGLE;
            }

            face.Color = DreamcastExporter::FaceColor(mesh, model, corners, perFace);

            Faces.push_back(face);
        }
    }
}

// How far out the geometry reaches, so the generated program can put the camera
// somewhere the scene is visible rather than at a distance picked in advance.
PRIVATE STATIC int DreamcastExporter::CameraDistance() {
    double worst = 0.0;

    for (size_t i = 0; i < Vertices.size(); i++) {
        double x = Vertices[i].X, y = Vertices[i].Y, z = Vertices[i].Z;
        double reach = sqrt(x * x + y * y + z * z);

        if (reach > worst)
            worst = reach;
    }

    int distance = (int)(worst * 3.0) + 32;

    if (distance < 64)
        distance = 64;
    if (distance > 16384)
        distance = 16384;

    return distance;
}

PUBLIC STATIC DreamcastExportResult DreamcastExporter::ExportScene3D(const char* outputPath, const char* scenePath) {
    DreamcastExportResult result;
    memset(&result, 0, sizeof(result));
    result.Is3D = true;

    Scene3DSettings settings;
    vector<Scene3DObject> objects;

    if (!Scene3DFormat::Read(scenePath, &settings, &objects)) {
        snprintf(result.Message, sizeof(result.Message), "Could not read the 3D scene \"%s\".", scenePath);
        return result;
    }

    if (!objects.size()) {
        StringUtils::Copy(result.Message, "That 3D scene has no models in it.", sizeof(result.Message));
        return result;
    }

    Vertices.clear();
    Faces.clear();
    DroppedFaces = 0;

    int loaded = 0;

    for (size_t i = 0; i < objects.size(); i++) {
        ResourceStream* stream = ResourceStream::New(objects[i].Source);
        if (!stream) {
            Log::Print(Log::LOG_WARN, "Dreamcast export: could not open model \"%s\".", objects[i].Source);
            continue;
        }

        IModel* model = new IModel();
        bool ok = model->Load(stream, objects[i].Source);
        stream->Close();

        if (!ok) {
            Log::Print(Log::LOG_WARN, "Dreamcast export: could not read model \"%s\".", objects[i].Source);
            delete model;
            continue;
        }

        DreamcastExporter::CollectModel(model, &objects[i]);
        delete model;

        loaded++;
    }

    result.ModelCount = loaded;
    result.VertexCount = (int)Vertices.size();
    result.FaceCount = (int)Faces.size();
    result.FacesDropped = DroppedFaces;

    if (!Faces.size()) {
        StringUtils::Copy(result.Message,
            "Nothing came out of that 3D scene. Its models either would not load or have no faces.",
            sizeof(result.Message));
        return result;
    }

    if (!DreamcastExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (result.FacesDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d model(s): %d vertices and %d faces. %d more face(s) went over what the SH-4 can transform in a frame and were left out.",
            result.ModelCount, result.VertexCount, result.FaceCount, result.FacesDropped);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d model(s): %d vertices and %d faces for the PowerVR.",
            result.ModelCount, result.VertexCount, result.FaceCount);
    }

    return result;
}

// ------------------------------------------------------------ 2D scenes ---

PUBLIC STATIC DreamcastExportResult DreamcastExporter::ExportScene(const char* outputPath) {
    DreamcastExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    result.LayerWidth = layer->Width * Scene::TileWidth;
    result.LayerHeight = layer->Height * Scene::TileHeight;

    // Never smaller than a screenful, so there is always something to draw.
    int width = result.LayerWidth < DREAMCAST_SCREEN_WIDTH ? DREAMCAST_SCREEN_WIDTH : result.LayerWidth;
    int height = result.LayerHeight < DREAMCAST_SCREEN_HEIGHT ? DREAMCAST_SCREEN_HEIGHT : result.LayerHeight;

    // Video memory is the ceiling here rather than the size of the picture:
    // every tile is a whole texture whether or not the layer fills it.
    bool clamped = false;
    for (;;) {
        int columns = (width + DREAMCAST_TILE_SIZE - 1) / DREAMCAST_TILE_SIZE;
        int rows = (height + DREAMCAST_TILE_SIZE - 1) / DREAMCAST_TILE_SIZE;
        size_t bytes = (size_t)columns * rows * DREAMCAST_TILE_SIZE * DREAMCAST_TILE_SIZE * 2;

        if (bytes <= DREAMCAST_MAX_TEXTURE_BYTES)
            break;

        if (height > DREAMCAST_SCREEN_HEIGHT)
            height = height / 2 < DREAMCAST_SCREEN_HEIGHT ? DREAMCAST_SCREEN_HEIGHT : height / 2;
        else if (width > DREAMCAST_SCREEN_WIDTH)
            width = width / 2 < DREAMCAST_SCREEN_WIDTH ? DREAMCAST_SCREEN_WIDTH : width / 2;
        else
            break;

        clamped = true;
    }

    result.ImageWidth = width;
    result.ImageHeight = height;

    DreamcastExporter::BuildTextures(layer, width, height);

    result.TileColumns = TileColumns;
    result.TileRows = TileRows;
    result.TextureBytes = Texels.size() * sizeof(Uint16);

    Vertices.clear();
    Faces.clear();

    if (!DreamcastExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (clamped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %dx%d of a %dx%d layer as %dx%d texture(s). The whole thing would not fit in video memory, so what was written is the top-left of it.",
            width, height, result.LayerWidth, result.LayerHeight, TileColumns, TileRows);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported a %dx%d picture as %dx%d texture(s) of %d, %d KB in sixteen-bit colour.",
            width, height, TileColumns, TileRows, DREAMCAST_TILE_SIZE,
            (int)(result.TextureBytes / 1024));
    }

    return result;
}

// -------------------------------------------------------------- writing ---

PRIVATE STATIC void DreamcastExporter::PushU32(vector<Uint8>* out, Uint32 value) {
    // Little endian: the Dreamcast's SH-4 runs that way round, which is the
    // opposite of every other SEGA machine this engine exports to.
    out->push_back((Uint8)(value & 0xFF));
    out->push_back((Uint8)((value >> 8) & 0xFF));
    out->push_back((Uint8)((value >> 16) & 0xFF));
    out->push_back((Uint8)((value >> 24) & 0xFF));
}

PRIVATE STATIC void DreamcastExporter::PushU16(vector<Uint8>* out, Uint16 value) {
    out->push_back((Uint8)(value & 0xFF));
    out->push_back((Uint8)((value >> 8) & 0xFF));
}

PRIVATE STATIC void DreamcastExporter::PushFloat(vector<Uint8>* out, float value) {
    Uint32 bits;
    memcpy(&bits, &value, sizeof(bits));
    DreamcastExporter::PushU32(out, bits);
}

PRIVATE STATIC bool DreamcastExporter::WriteProject(const char* outputPath, DreamcastExportResult* result) {
    char path[1024];
    char runtime[1024];

    if (!DreamcastExporter::FindRuntime(runtime, sizeof(runtime))) {
        StringUtils::Copy(result->Message,
            "Could not find the Dreamcast runtime. It ships as meta/dreamcast/runtime beside the engine; point --dreamcast-runtime at it if it is somewhere else.",
            sizeof(result->Message));
        return false;
    }

    const char* dirs[3] = { "", "/romdisk", "/src" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, dirs[i]);
        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    static const char* srcFiles[4] = { "dreamcast.h", "scene2d.c", "scene3d.c", "pad.c" };

    char from[1024];
    for (int i = 0; i < 4; i++) {
        snprintf(from, sizeof(from), "%s/%s", runtime, srcFiles[i]);
        snprintf(path, sizeof(path), "%s/src/%s", outputPath, srcFiles[i]);
        if (!DreamcastExporter::CopyFile(from, path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
            return false;
        }
    }

    // The data goes in a romdisk, which KallistiOS links into the binary and
    // mounts at /rd -- so the program has real files to open and the disc image
    // does not have to carry a filesystem of its own.
    vector<Uint8> blob;

    if (result->Is3D) {
        blob.push_back('H'); blob.push_back('D'); blob.push_back('C'); blob.push_back('3');
        DreamcastExporter::PushU32(&blob, (Uint32)Vertices.size());
        DreamcastExporter::PushU32(&blob, (Uint32)Faces.size());

        for (size_t i = 0; i < Vertices.size(); i++) {
            DreamcastExporter::PushFloat(&blob, Vertices[i].X);
            DreamcastExporter::PushFloat(&blob, Vertices[i].Y);
            DreamcastExporter::PushFloat(&blob, Vertices[i].Z);
        }

        for (size_t i = 0; i < Faces.size(); i++) {
            DreamcastExporter::PushU16(&blob, Faces[i].A);
            DreamcastExporter::PushU16(&blob, Faces[i].B);
            DreamcastExporter::PushU16(&blob, Faces[i].C);
            DreamcastExporter::PushU16(&blob, Faces[i].D);
            DreamcastExporter::PushU32(&blob, Faces[i].Color);
            DreamcastExporter::PushU16(&blob, Faces[i].Flags);
            DreamcastExporter::PushU16(&blob, 0);
        }

        snprintf(path, sizeof(path), "%s/romdisk/mesh.bin", outputPath);
    }
    else {
        blob.push_back('H'); blob.push_back('D'); blob.push_back('C'); blob.push_back('2');
        DreamcastExporter::PushU32(&blob, (Uint32)result->ImageWidth);
        DreamcastExporter::PushU32(&blob, (Uint32)result->ImageHeight);
        DreamcastExporter::PushU32(&blob, (Uint32)DREAMCAST_TILE_SIZE);
        DreamcastExporter::PushU32(&blob, (Uint32)TileColumns);
        DreamcastExporter::PushU32(&blob, (Uint32)TileRows);

        for (size_t i = 0; i < Texels.size(); i++)
            DreamcastExporter::PushU16(&blob, Texels[i]);

        snprintf(path, sizeof(path), "%s/romdisk/scene.bin", outputPath);
    }

    if (!DreamcastExporter::WriteBinary(path, blob.data(), blob.size())) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return DreamcastExporter::WriteSources(outputPath, result);
}

PRIVATE STATIC bool DreamcastExporter::WriteSources(const char* outputPath, DreamcastExportResult* result) {
    char path[1024];
    char text[8192];

    if (result->Is3D) {
        snprintf(text, sizeof(text),
            "/* Generated by the Hatch Game Engine's SEGA Dreamcast exporter.\n"
            " *\n"
            " * The scene's geometry is in the romdisk, already in world space. The\n"
            " * SH-4 transforms it and the PowerVR draws it; the pad turns it.\n"
            " */\n"
            "\n"
            "#include \"dreamcast.h\"\n"
            "\n"
            "int main(int argc, char* argv[]) {\n"
            "    Mesh3D mesh;\n"
            "    float yaw = 0.0f, pitch = 0.4f;\n"
            "    float distance = %d.0f;\n"
            "\n"
            "    (void)argc; (void)argv;\n"
            "\n"
            "    pvr_init_defaults();\n"
            "\n"
            "    if (!mesh3d_load(&mesh, \"/rd/mesh.bin\"))\n"
            "        return 1;\n"
            "\n"
            "    while (1) {\n"
            "        uint32_t pad = pad_read();\n"
            "\n"
            "        if (pad & CONT_START)\n"
            "            break;\n"
            "\n"
            "        if (pad & CONT_DPAD_LEFT)  yaw -= 0.04f;\n"
            "        if (pad & CONT_DPAD_RIGHT) yaw += 0.04f;\n"
            "        if (pad & CONT_DPAD_UP)    pitch -= 0.04f;\n"
            "        if (pad & CONT_DPAD_DOWN)  pitch += 0.04f;\n"
            "        if (pad & CONT_A)          distance -= 4.0f;\n"
            "        if (pad & CONT_B)          distance += 4.0f;\n"
            "\n"
            "        if (distance < 16.0f)\n"
            "            distance = 16.0f;\n"
            "\n"
            "        /* Nothing on the pad turns it, so it turns by itself. */\n"
            "        if (!(pad & (CONT_DPAD_LEFT | CONT_DPAD_RIGHT)))\n"
            "            yaw += 0.01f;\n"
            "\n"
            "        pvr_wait_ready();\n"
            "        pvr_scene_begin();\n"
            "        pvr_list_begin(PVR_LIST_OP_POLY);\n"
            "\n"
            "        mesh3d_draw(&mesh, yaw, pitch, distance);\n"
            "\n"
            "        pvr_list_finish();\n"
            "        pvr_scene_finish();\n"
            "    }\n"
            "\n"
            "    mesh3d_free(&mesh);\n"
            "\n"
            "    return 0;\n"
            "}\n",
            DreamcastExporter::CameraDistance());
    }
    else {
        snprintf(text, sizeof(text),
            "/* Generated by the Hatch Game Engine's SEGA Dreamcast exporter.\n"
            " *\n"
            " * The scene layer is in the romdisk as sixteen-bit textures. The\n"
            " * PowerVR draws the ones the camera is over; the pad moves it.\n"
            " */\n"
            "\n"
            "#include \"dreamcast.h\"\n"
            "\n"
            "int main(int argc, char* argv[]) {\n"
            "    Scene2D scene;\n"
            "    float cameraX = 0.0f, cameraY = 0.0f;\n"
            "\n"
            "    (void)argc; (void)argv;\n"
            "\n"
            "    pvr_init_defaults();\n"
            "\n"
            "    if (!scene2d_load(&scene, \"/rd/scene.bin\"))\n"
            "        return 1;\n"
            "\n"
            "    while (1) {\n"
            "        uint32_t pad = pad_read();\n"
            "\n"
            "        if (pad & CONT_START)\n"
            "            break;\n"
            "\n"
            "        if (pad & CONT_DPAD_LEFT)  cameraX -= 4.0f;\n"
            "        if (pad & CONT_DPAD_RIGHT) cameraX += 4.0f;\n"
            "        if (pad & CONT_DPAD_UP)    cameraY -= 4.0f;\n"
            "        if (pad & CONT_DPAD_DOWN)  cameraY += 4.0f;\n"
            "\n"
            "        if (cameraX < 0.0f) cameraX = 0.0f;\n"
            "        if (cameraY < 0.0f) cameraY = 0.0f;\n"
            "        if (cameraX > scene.Width - SCREEN_W) cameraX = scene.Width - SCREEN_W;\n"
            "        if (cameraY > scene.Height - SCREEN_H) cameraY = scene.Height - SCREEN_H;\n"
            "        if (cameraX < 0.0f) cameraX = 0.0f;\n"
            "        if (cameraY < 0.0f) cameraY = 0.0f;\n"
            "\n"
            "        pvr_wait_ready();\n"
            "        pvr_scene_begin();\n"
            "        pvr_list_begin(PVR_LIST_OP_POLY);\n"
            "\n"
            "        scene2d_draw(&scene, cameraX, cameraY);\n"
            "\n"
            "        pvr_list_finish();\n"
            "        pvr_scene_finish();\n"
            "    }\n"
            "\n"
            "    scene2d_free(&scene);\n"
            "\n"
            "    return 0;\n"
            "}\n");
    }

    snprintf(path, sizeof(path), "%s/src/main.c", outputPath);
    if (!DreamcastExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the Makefile ---
    //
    // KallistiOS builds through environment it sets up itself, so this defers
    // to its Makefile.rules rather than naming a compiler. The romdisk is how
    // KOS gets data files into a binary: genromfs makes an image, bin2o turns
    // it into an object, and KOS mounts it at /rd.
    snprintf(text, sizeof(text),
        "# %s, for the SEGA Dreamcast.\n"
        "#\n"
        "# Needs KallistiOS: https://github.com/KallistiOS/KallistiOS\n"
        "#\n"
        "#   source /opt/toolchains/dc/kos/environ.sh\n"
        "#   make\n"
        "#\n"
        "# hatch.elf is what an emulator will boot. 'make dist' also writes\n"
        "# 1ST_READ.BIN, which is what goes on a disc.\n"
        "\n"
        "TARGET = hatch.elf\n"
        "OBJS = src/main.o src/scene2d.o src/scene3d.o src/pad.o romdisk.o\n"
        "\n"
        "KOS_ROMDISK_DIR = romdisk\n"
        "\n"
        "all: rm-elf $(TARGET)\n"
        "\n"
        "include $(KOS_BASE)/Makefile.rules\n"
        "\n"
        "clean: rm-elf\n"
        "\t-rm -f $(OBJS)\n"
        "\n"
        "rm-elf:\n"
        "\t-rm -f $(TARGET) romdisk.*\n"
        "\n"
        "$(TARGET): $(OBJS)\n"
        "\tkos-cc -o $(TARGET) $(OBJS) -lm\n"
        "\n"
        "dist: $(TARGET)\n"
        "\t$(KOS_STRIP) $(TARGET)\n"
        "\tkos-objcopy -O binary $(TARGET) 1ST_READ.BIN\n"
        "\n"
        ".PHONY: all clean dist rm-elf\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene");

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!DreamcastExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return DreamcastExporter::WriteReadme(outputPath, result);
}

PRIVATE STATIC bool DreamcastExporter::WriteReadme(const char* outputPath, DreamcastExportResult* result) {
    char path[1024];
    char text[8192];

    const char* name = Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene";

    if (result->Is3D) {
        snprintf(text, sizeof(text),
            "# %s, for the SEGA Dreamcast\n"
            "\n"
            "Exported from the Hatch Game Engine. This is the 3D scene, drawn on the\n"
            "PowerVR2.\n"
            "\n"
            "| File | What it holds |\n"
            "| --- | --- |\n"
            "| `romdisk/mesh.bin` | %d vertices and %d faces, in world space, as floats |\n"
            "| `src/main.c` | the program that turns and draws it |\n"
            "| `src/scene3d.c` | the transform and the PowerVR submission |\n"
            "\n"
            "%d model(s) came across.\n"
            "\n"
            "## Building\n"
            "\n"
            "```sh\n"
            "source /opt/toolchains/dc/kos/environ.sh\n"
            "make\n"
            "```\n"
            "\n"
            "`hatch.elf` is the result, and an emulator will boot it directly.\n"
            "`make dist` also writes `1ST_READ.BIN` for a disc.\n"
            "\n"
            "## How it draws\n"
            "\n"
            "Compare this with the Saturn export and the machine is the whole story.\n"
            "The Saturn's SH-2 had no floating point, so its geometry went over as\n"
            "16.16 fixed point; the SH-4 here has an FPU, so this is ordinary floats.\n"
            "\n"
            "The Saturn had no depth buffer either, so every face had to be sorted\n"
            "back to front each frame and drawn in that order. The PowerVR is a\n"
            "tile-based deferred renderer that resolves opaque geometry per pixel in\n"
            "hardware, so there is no sort here at all -- faces are submitted in\n"
            "whatever order they are stored and come out right.\n"
            "\n"
            "Backfaces are still dropped in software. The PVR can cull, but on the\n"
            "winding its tile accelerator sees, and the winding here is the engine's.\n"
            "\n"
            "Faces are flat shaded, in the model's vertex colours averaged over the\n"
            "face or its material's diffuse colour. The PVR does gouraud and textures\n"
            "as a matter of course, so that is a thing to add rather than a limit.\n"
            "\n"
            "## What this is and is not\n"
            "\n"
            "The pad turns the scene and Start quits. Hatch's game logic does not come\n"
            "across -- it is bytecode for a VM that is not here. The geometry is.\n"
            "\n"
            "This one does use a library: KallistiOS, which is the Dreamcast's\n"
            "homebrew operating system. The older SEGA exports write hardware\n"
            "registers directly because there is nothing else to use; here there is,\n"
            "and reimplementing it would make a worse export rather than a purer one.\n",
            name, result->VertexCount, result->FaceCount, result->ModelCount);
    }
    else {
        snprintf(text, sizeof(text),
            "# %s, for the SEGA Dreamcast\n"
            "\n"
            "Exported from the Hatch Game Engine. The scene layer is a set of PowerVR\n"
            "textures, drawn as quads and scrolled with the pad.\n"
            "\n"
            "| File | What it holds |\n"
            "| --- | --- |\n"
            "| `romdisk/scene.bin` | a %dx%d picture as %dx%d textures of %d, RGB565, %d KB |\n"
            "| `src/main.c` | the program that shows it |\n"
            "| `src/scene2d.c` | the loading and the PowerVR submission |\n"
            "\n"
            "## Building\n"
            "\n"
            "```sh\n"
            "source /opt/toolchains/dc/kos/environ.sh\n"
            "make\n"
            "```\n"
            "\n"
            "`hatch.elf` is the result, and an emulator will boot it directly.\n"
            "`make dist` also writes `1ST_READ.BIN` for a disc.\n"
            "\n"
            "## What came across, and what did not\n"
            "\n"
            "All of the colour. This is the one SEGA target where the art is not cut\n"
            "down to fit: no palette of sixteen, no three bits a channel, no tile\n"
            "deduplication. Sixteen bits a pixel, which is what the PowerVR reads.\n"
            "\n"
            "The layer is cut into squares because the PowerVR wants its textures a\n"
            "power of two on a side and will not take one larger than 1024. A square\n"
            "that hangs off the edge of the layer is still a whole texture, with the\n"
            "part past the edge left black.\n"
            "\n"
            "Game logic does not come across -- it is bytecode for a VM that is not\n"
            "here.\n",
            name, result->ImageWidth, result->ImageHeight,
            result->TileColumns, result->TileRows, DREAMCAST_TILE_SIZE,
            (int)(result->TextureBytes / 1024));
    }

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!DreamcastExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
