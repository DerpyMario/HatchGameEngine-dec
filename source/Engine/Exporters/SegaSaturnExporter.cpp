#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/SegaSaturnTypes.h>
#include <Engine/Rendering/Scene3DTypes.h>

need_t SceneLayer;
need_t Scene3DObject;
need_t Scene3DSettings;
need_t Mesh;
need_t IModel;

class SegaSaturnExporter {
public:

};
#endif

#include <Engine/Exporters/SegaSaturnExporter.h>
#include <Engine/Exporters/SegaSceneArt.h>

#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/Math/Matrix4x4.h>
#include <Engine/Rendering/Material.h>
#include <Engine/Rendering/Mesh.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/ResourceTypes/IModel.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/ResourceTypes/SceneFormats/Scene3DFormat.h>
#include <Engine/IO/ResourceStream.h>
#include <Engine/Scene.h>
#include <Engine/Scene/SceneLayer.h>
#include <Engine/Utilities/StringUtils.h>

// Turning a Hatch scene into something a SEGA Saturn can show.
//
// The Saturn has two video chips and this uses both. A tile scene becomes a
// VDP2 bitmap -- eight bits a pixel out of a 256-colour CRAM -- because that is
// what VDP2 backgrounds are. A 3D scene becomes a table of vertices and faces
// that the SH-2 transforms and hands to VDP1 as polygon commands, because that
// is how the Saturn drew polygons and there is no other way to do it.
//
// Nothing here needs SEGA's own libraries. SGL would give you a matrix stack
// and a scene graph, but it cannot be redistributed, and an export nobody can
// build is not an export. What comes out is bare metal: sh-elf-gcc, a linker
// script, and registers written directly.

static vector<Uint8>  Indices;      // one byte a pixel, row major
static vector<Uint32> Palette;      // 0xRRGGBB, already rounded to five bits
static int            DroppedColors;

static vector<SaturnVertex> Vertices;
static vector<SaturnFace>   Faces;
static int                  DroppedFaces;

// How far out the geometry reaches from the origin. The generated program uses
// it to put the camera somewhere the scene is actually visible: a fixed
// distance either buries a large scene in the near plane or leaves a small one
// as a dot.
PRIVATE STATIC int SegaSaturnExporter::CameraDistance() {
    double worst = 0.0;

    for (size_t i = 0; i < Vertices.size(); i++) {
        double x = (double)Vertices[i].X / 65536.0;
        double y = (double)Vertices[i].Y / 65536.0;
        double z = (double)Vertices[i].Z / 65536.0;

        double distance = sqrt(x * x + y * y + z * z);
        if (distance > worst)
            worst = distance;
    }

    // Three radii back puts the whole of a scene inside a 352 pixel screen at
    // the focal length the runtime uses, with room to turn.
    int distance = (int)(worst * 3.0) + 32;

    if (distance < 64)
        distance = 64;
    if (distance > 8192)
        distance = 8192;

    return distance;
}

// Rounded into the five bits a channel the Saturn keeps, then spread back over
// the full range so comparisons happen in the space it will land in.
PRIVATE STATIC Uint32 SegaSaturnExporter::QuantizeColor(Uint32 argb) {
    Uint32 r = (Uint32)((((argb >> 16) & 0xFF) >> 3) * 255 / 31);
    Uint32 g = (Uint32)((((argb >> 8) & 0xFF) >> 3) * 255 / 31);
    Uint32 b = (Uint32)(((argb & 0xFF) >> 3) * 255 / 31);

    return (r << 16) | (g << 8) | b;
}

PRIVATE STATIC long SegaSaturnExporter::ColorDistance(Uint32 a, Uint32 b) {
    long dr = (long)((a >> 16) & 0xFF) - (long)((b >> 16) & 0xFF);
    long dg = (long)((a >> 8) & 0xFF) - (long)((b >> 8) & 0xFF);
    long db = (long)(a & 0xFF) - (long)(b & 0xFF);

    return dr * dr + dg * dg + db * db;
}

PRIVATE STATIC int SegaSaturnExporter::NearestInPalette(Uint32 color) {
    int best = 0;
    long bestDistance = -1;

    for (size_t i = 0; i < Palette.size(); i++) {
        long distance = SegaSaturnExporter::ColorDistance(Palette[i], color);
        if (bestDistance < 0 || distance < bestDistance) {
            bestDistance = distance;
            best = (int)i;
        }
    }

    return best;
}

// Counts every colour the picture uses, most-used first. Ordering by use is
// what makes the reduction below defensible: when something has to go, it is
// the colour covering the fewest pixels.
PRIVATE STATIC void SegaSaturnExporter::GatherColors(SceneLayer* layer, int width, int height, vector<SaturnColorUse>* out) {
    std::map<Uint32, size_t> counts;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            counts[SegaSaturnExporter::QuantizeColor(pixel & 0xFFFFFF)]++;
        }
    }

    out->clear();
    for (std::map<Uint32, size_t>::iterator it = counts.begin(); it != counts.end(); it++) {
        SaturnColorUse use;
        use.Color = it->first;
        use.Count = it->second;
        out->push_back(use);
    }

    std::sort(out->begin(), out->end(), [](const SaturnColorUse& a, const SaturnColorUse& b) -> bool {
        if (a.Count != b.Count)
            return a.Count > b.Count;

        // Ties broken by value, so two runs over one scene agree.
        return a.Color < b.Color;
    });
}

// Draws the layer into one byte a pixel. Index 0 stays transparent, so art is
// laid over 1..255 and anything that did not fit is matched to the nearest that
// did.
PRIVATE STATIC void SegaSaturnExporter::BuildImage(SceneLayer* layer, int width, int height) {
    Indices.assign((size_t)width * height, 0);

    std::map<Uint32, int> resolved;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            Uint32 pixel;
            if (!SegaSceneArt::GetLayerPixel(layer, x, y, &pixel))
                continue;

            if (((pixel >> 24) & 0xFF) < 128)
                continue;

            Uint32 color = SegaSaturnExporter::QuantizeColor(pixel & 0xFFFFFF);

            std::map<Uint32, int>::iterator known = resolved.find(color);
            int index;

            if (known != resolved.end())
                index = known->second;
            else {
                index = -1;
                for (size_t i = 0; i < Palette.size(); i++) {
                    if (Palette[i] == color) {
                        index = (int)i + 1;
                        break;
                    }
                }

                // Not in the palette, so it was one of the ones reduced away.
                if (index < 0) {
                    index = SegaSaturnExporter::NearestInPalette(color) + 1;
                    DroppedColors++;
                }

                resolved[color] = index;
            }

            Indices[(size_t)x + (size_t)y * width] = (Uint8)index;
        }
    }
}

PRIVATE STATIC bool SegaSaturnExporter::WriteBinary(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool SegaSaturnExporter::WriteText(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;

    bool ok = fputs(text, f) >= 0;
    fclose(f);

    return ok;
}

PRIVATE STATIC bool SegaSaturnExporter::CopyFile(const char* from, const char* to) {
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

// The runtime lives beside the engine rather than inside it -- it is startup
// code, a linker script and register pokes that never change with the scene,
// and it reads better as files than as string literals.
//
// An installed engine has meta/ next to it, so that is looked for first from
// where the engine was started and then from where the engine itself is. A
// build tree does not necessarily have either -- the binary can be anywhere and
// the working directory is usually the project being exported -- so
// --saturn-runtime says outright where it is.
PRIVATE STATIC bool SegaSaturnExporter::FindRuntime(char* out, size_t outSize) {
    if (Application::SegaSaturnRuntimePath.size()) {
        StringUtils::Copy(out, Application::SegaSaturnRuntimePath.c_str(), outSize);
        return Directory::Exists(out);
    }

    const char* relative = "meta/saturn/runtime";

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

// A title as the Saturn's disc header wants it: fixed width, uppercase ASCII,
// space padded, no terminator. Anything else is written as a space rather than
// left to run off the end of a field the console reads by offset.
PRIVATE STATIC void SegaSaturnExporter::HeaderField(char* out, size_t width, const char* text) {
    size_t i = 0;

    if (text) {
        for (; i < width && text[i]; i++) {
            char c = text[i];

            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == ' ' || c == '-' || c == '_' || c == '.'))
                c = ' ';

            out[i] = c;
        }
    }

    for (; i < width; i++)
        out[i] = ' ';

    out[width] = '\0';
}

// ------------------------------------------------------------ 3D scenes ---

PRIVATE STATIC Uint16 SegaSaturnExporter::ColorToSaturn(Uint32 argb) {
    return SATURN_COLOR_WORD((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

// The colour a face is drawn in.
//
// A mesh may carry a colour per vertex; the Saturn draws a flat polygon, so the
// corners are averaged into the one colour it can use. Failing that the model's
// material has a diffuse colour, and failing that everything would be black,
// which reads as a bug rather than as a model -- so it falls back to grey.
PRIVATE STATIC Uint16 SegaSaturnExporter::FaceColor(Mesh* mesh, IModel* model, Sint32* corners, int cornerCount) {
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

        return SATURN_COLOR_WORD(r, g, b);
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
                return SATURN_COLOR_WORD(r, g, b);
        }
    }

    return SATURN_COLOR_WORD(160, 160, 160);
}

// Walks one placed model into the shared vertex and face tables, with its
// position, rotation and scale already applied. The console gets world space
// and never has to know a scene graph existed.
PRIVATE STATIC void SegaSaturnExporter::CollectModel(IModel* model, Scene3DObject* object) {
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

        if (base + mesh->VertexCount > SATURN_MAX_VERTICES)
            break;

        for (Uint32 v = 0; v < mesh->VertexCount; v++) {
            // The engine keeps positions in 16.16 already, which is the same
            // fixed point the SH-2 side works in, so this is a matrix multiply
            // and not a conversion.
            float x = (float)mesh->PositionBuffer[v].X / 65536.0f;
            float y = (float)mesh->PositionBuffer[v].Y / 65536.0f;
            float z = (float)mesh->PositionBuffer[v].Z / 65536.0f;

            // Matrix4x4 is column major, the way OpenGL lays one out: element
            // (row i, column j) is Values[j * 4 + i], and the translation is
            // the last column at 12, 13 and 14. Reading it as row major loses
            // the translation entirely -- every model lands on the origin --
            // and quietly transposes the rotation, which a symmetrical model
            // hides.
            float* M = transform.Values;
            float wx = M[0] * x + M[4] * y + M[8] * z + M[12];
            float wy = M[1] * x + M[5] * y + M[9] * z + M[13];
            float wz = M[2] * x + M[6] * y + M[10] * z + M[14];

            SaturnVertex vertex;
            vertex.X = (Sint32)(wx * 65536.0f);
            vertex.Y = (Sint32)(wy * 65536.0f);
            vertex.Z = (Sint32)(wz * 65536.0f);

            Vertices.push_back(vertex);
        }

        int perFace = model->VertexPerFace ? model->VertexPerFace : 3;

        for (Uint32 i = 0; i + perFace <= mesh->VertexIndexCount; i += perFace) {
            if (Faces.size() >= SATURN_MAX_FACES) {
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

            SaturnFace face;
            face.A = (Uint16)(base + corners[0]);
            face.B = (Uint16)(base + corners[1]);
            face.C = (Uint16)(base + corners[2]);

            if (perFace >= 4) {
                face.D = (Uint16)(base + corners[3]);
                face.Flags = 0;
            }
            else {
                // VDP1 draws quads. A triangle is one whose last two corners
                // are the same point, which is how the Saturn always did them.
                face.D = face.C;
                face.Flags = SATURN_FACE_TRIANGLE;
            }

            face.Color = SegaSaturnExporter::FaceColor(mesh, model, corners, perFace);

            Faces.push_back(face);
        }
    }
}

PUBLIC STATIC SegaSaturnExportResult SegaSaturnExporter::ExportScene3D(const char* outputPath, const char* scenePath) {
    SegaSaturnExportResult result;
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
            Log::Print(Log::LOG_WARN, "Saturn export: could not open model \"%s\".", objects[i].Source);
            continue;
        }

        IModel* model = new IModel();
        bool ok = model->Load(stream, objects[i].Source);
        stream->Close();

        if (!ok) {
            Log::Print(Log::LOG_WARN, "Saturn export: could not read model \"%s\".", objects[i].Source);
            delete model;
            continue;
        }

        SegaSaturnExporter::CollectModel(model, &objects[i]);
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

    if (!SegaSaturnExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (result.FacesDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d model(s): %d vertices and %d faces. %d more face(s) went over what the SH-2 can transform in a frame and were left out.",
            result.ModelCount, result.VertexCount, result.FaceCount, result.FacesDropped);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %d model(s): %d vertices and %d faces for VDP1.",
            result.ModelCount, result.VertexCount, result.FaceCount);
    }

    return result;
}

// ------------------------------------------------------------ 2D scenes ---

PUBLIC STATIC SegaSaturnExportResult SegaSaturnExporter::ExportScene(const char* outputPath) {
    SegaSaturnExportResult result;
    memset(&result, 0, sizeof(result));

    SceneLayer* layer = SegaSceneArt::PickLayer();
    if (!layer) {
        StringUtils::Copy(result.Message, "The scene has no visible tile layer to export.", sizeof(result.Message));
        return result;
    }

    result.LayerWidth = layer->Width * Scene::TileWidth;
    result.LayerHeight = layer->Height * Scene::TileHeight;

    // Never smaller than a screenful, so the runtime always has something to
    // blit, and never so large that the picture will not fit in memory.
    int width = result.LayerWidth < SATURN_SCREEN_WIDTH ? SATURN_SCREEN_WIDTH : result.LayerWidth;
    int height = result.LayerHeight < SATURN_SCREEN_HEIGHT ? SATURN_SCREEN_HEIGHT : result.LayerHeight;

    bool clamped = false;
    while ((size_t)width * height > SATURN_MAX_IMAGE_BYTES) {
        if (height > SATURN_SCREEN_HEIGHT)
            height = height / 2 < SATURN_SCREEN_HEIGHT ? SATURN_SCREEN_HEIGHT : height / 2;
        else if (width > SATURN_SCREEN_WIDTH)
            width = width / 2 < SATURN_SCREEN_WIDTH ? SATURN_SCREEN_WIDTH : width / 2;
        else
            break;

        clamped = true;
    }

    result.ImageWidth = width;
    result.ImageHeight = height;
    result.ImageBytes = (size_t)width * height;

    vector<SaturnColorUse> colors;
    SegaSaturnExporter::GatherColors(layer, width, height, &colors);

    result.ColorsFound = (int)colors.size();

    Palette.clear();
    size_t keep = colors.size() < SATURN_PALETTE_USABLE ? colors.size() : SATURN_PALETTE_USABLE;
    for (size_t i = 0; i < keep; i++)
        Palette.push_back(colors[i].Color);

    result.PaletteCount = (int)Palette.size();

    DroppedColors = 0;
    SegaSaturnExporter::BuildImage(layer, width, height);
    result.ColorsDropped = DroppedColors;

    Vertices.clear();
    Faces.clear();

    if (!SegaSaturnExporter::WriteProject(outputPath, &result))
        return result;

    result.Success = true;

    if (clamped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported %dx%d of a %dx%d layer in %d colour(s). The whole thing would not fit in memory, so what was written is the top-left of it.",
            width, height, result.LayerWidth, result.LayerHeight, result.PaletteCount);
    }
    else if (result.ColorsDropped) {
        snprintf(result.Message, sizeof(result.Message),
            "Exported a %dx%d picture. %d of %d colour(s) did not fit a palette of 255 and were matched to the nearest that did.",
            width, height, result.ColorsDropped, result.ColorsFound);
    }
    else {
        snprintf(result.Message, sizeof(result.Message),
            "Exported a %dx%d picture in %d colour(s), %d bytes.",
            width, height, result.PaletteCount, (int)result.ImageBytes);
    }

    return result;
}

// -------------------------------------------------------------- writing ---

PRIVATE STATIC bool SegaSaturnExporter::WriteProject(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char runtime[1024];

    if (!SegaSaturnExporter::FindRuntime(runtime, sizeof(runtime))) {
        StringUtils::Copy(result->Message,
            "Could not find the Saturn runtime. It ships as meta/saturn/runtime beside the engine; point --saturn-runtime at it if it is somewhere else.",
            sizeof(result->Message));
        return false;
    }

    const char* dirs[3] = { "", "/res", "/src" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s%s", outputPath, dirs[i]);
        if (!Directory::Exists(path) && !Directory::CreatePath(path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not create \"%s\".", path);
            return false;
        }
    }

    // --- the runtime, copied in as it is ---
    static const char* srcFiles[7] = {
        "saturn.h", "vdp.c", "pad.c", "string.c", "scene3d.c", "crt0.s", "ip.s"
    };

    char from[1024];
    for (int i = 0; i < 7; i++) {
        snprintf(from, sizeof(from), "%s/%s", runtime, srcFiles[i]);
        snprintf(path, sizeof(path), "%s/src/%s", outputPath, srcFiles[i]);
        if (!SegaSaturnExporter::CopyFile(from, path)) {
            snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
            return false;
        }
    }

    snprintf(from, sizeof(from), "%s/saturn.ld", runtime);
    snprintf(path, sizeof(path), "%s/src/saturn.ld", outputPath);
    if (!SegaSaturnExporter::CopyFile(from, path)) {
        snprintf(result->Message, sizeof(result->Message), "Could not copy \"%s\".", from);
        return false;
    }

    if (result->Is3D) {
        // --- the mesh, big endian, which is the SH-2's own byte order ---
        vector<Uint8> blob;

        blob.push_back('H'); blob.push_back('S'); blob.push_back('M'); blob.push_back('1');

        Uint32 counts[2] = { (Uint32)Vertices.size(), (Uint32)Faces.size() };
        for (int c = 0; c < 2; c++) {
            blob.push_back((Uint8)(counts[c] >> 24));
            blob.push_back((Uint8)(counts[c] >> 16));
            blob.push_back((Uint8)(counts[c] >> 8));
            blob.push_back((Uint8)counts[c]);
        }

        for (size_t i = 0; i < Vertices.size(); i++) {
            Sint32 axis[3] = { Vertices[i].X, Vertices[i].Y, Vertices[i].Z };
            for (int a = 0; a < 3; a++) {
                Uint32 v = (Uint32)axis[a];
                blob.push_back((Uint8)(v >> 24));
                blob.push_back((Uint8)(v >> 16));
                blob.push_back((Uint8)(v >> 8));
                blob.push_back((Uint8)v);
            }
        }

        for (size_t i = 0; i < Faces.size(); i++) {
            Uint16 fields[6] = {
                Faces[i].A, Faces[i].B, Faces[i].C, Faces[i].D,
                Faces[i].Color, Faces[i].Flags
            };

            for (int f = 0; f < 6; f++) {
                blob.push_back((Uint8)(fields[f] >> 8));
                blob.push_back((Uint8)fields[f]);
            }
        }

        snprintf(path, sizeof(path), "%s/res/mesh.bin", outputPath);
        if (!SegaSaturnExporter::WriteBinary(path, blob.data(), blob.size())) {
            snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
            return false;
        }
    }
    else {
        // --- the palette, as the Saturn's own colour words, big endian ---
        vector<Uint8> bytes;

        // Entry zero is left at black: nothing indexes it, since it is the
        // index a VDP2 background treats as transparent.
        bytes.push_back(0);
        bytes.push_back(0);

        for (size_t i = 0; i < SATURN_PALETTE_USABLE; i++) {
            Uint16 word = 0;
            if (i < Palette.size()) {
                Uint32 color = Palette[i];
                word = SATURN_COLOR_WORD((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
            }

            bytes.push_back((Uint8)(word >> 8));
            bytes.push_back((Uint8)(word & 0xFF));
        }

        snprintf(path, sizeof(path), "%s/res/palette.bin", outputPath);
        if (!SegaSaturnExporter::WriteBinary(path, bytes.data(), bytes.size())) {
            snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
            return false;
        }

        // --- the picture, one byte a pixel ---
        snprintf(path, sizeof(path), "%s/res/image.bin", outputPath);
        if (!SegaSaturnExporter::WriteBinary(path, Indices.data(), Indices.size())) {
            snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
            return false;
        }
    }

    return SegaSaturnExporter::WriteSources(outputPath, result);
}

PRIVATE STATIC bool SegaSaturnExporter::WriteIPHeader(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char text[4096];
    char title[113];
    char product[11];

    const char* sceneName = Scene::CurrentScene[0] ? Scene::CurrentScene : "HATCH SCENE";

    SegaSaturnExporter::HeaderField(title, 112, sceneName);
    SegaSaturnExporter::HeaderField(product, 10, "T-000");

    // The console reads this by byte offset, so every field is a fixed width
    // and the assembler is told so rather than trusted to count.
    snprintf(text, sizeof(text),
        "! The Saturn disc header for this export, byte for byte as the console\n"
        "! reads it. Written by the Hatch Game Engine; edit the title if you like,\n"
        "! but keep every field exactly the width it is.\n"
        "\n"
        "    .ascii  \"SEGA SEGASATURN \"\n"
        "    .ascii  \"SEGA TP T-000   \"\n"
        "    .ascii  \"%s\"\n"
        "    .ascii  \"V1.000\"\n"
        "    .ascii  \"20260101\"\n"
        "    .ascii  \"CD-1/1  \"\n"
        "    .ascii  \"JTUBKAEL  \"\n"
        "    .ascii  \"      \"\n"
        "    .ascii  \"J       \"\n"
        "    .ascii  \"        \"\n"
        "    .ascii  \"%s\"\n"
        "    .space  16, 0\n"
        "    .long   0x00001000        ! how much of IP.BIN to load\n"
        "    .long   0x00000000\n"
        "    .long   0x06002000        ! master SH-2 stack\n"
        "    .long   0x06001E00        ! slave SH-2 stack\n"
        "    .long   0x06004000        ! where the program goes\n"
        "    .long   0x00000000        ! and how big it is -- 0 means all of it\n"
        "    .long   0x00000000\n"
        "    .long   0x00000000\n",
        product, title);

    snprintf(path, sizeof(path), "%s/src/ip_header.inc", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}

PRIVATE STATIC bool SegaSaturnExporter::WriteSources(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char text[8192];

    if (!SegaSaturnExporter::WriteIPHeader(outputPath, result))
        return false;

    // --- main.c ---
    if (result->Is3D) {
        snprintf(text, sizeof(text),
            "/* Generated by the Hatch Game Engine's SEGA Saturn exporter.\n"
            " *\n"
            " * The scene's geometry is in res/mesh.bin, already in world space and\n"
            " * already in the 16.16 fixed point the SH-2 works in. Every frame it is\n"
            " * rotated, projected, culled, sorted back to front and handed to VDP1 as\n"
            " * polygon commands. The pad turns it; the game goes here.\n"
            " */\n"
            "\n"
            "#include \"saturn.h\"\n"
            "\n"
            "extern const unsigned char scene_mesh[];\n"
            "\n"
            "int main(void) {\n"
            "    Mesh3D mesh;\n"
            "    int yaw = 0, pitch = 16;\n"
            "    s32 distance = FIX(%d);\n"
            "\n"
            "    vdp_init_bitmap();\n"
            "    vdp1_init();\n"
            "    pad_init();\n"
            "\n"
            "    if (!mesh3d_open(&mesh, scene_mesh)) {\n"
            "        /* The blob is not what this build expects. Sitting here is more\n"
            "         * use than drawing whatever the bytes happen to mean. */\n"
            "        while (1)\n"
            "            vdp_wait_vblank();\n"
            "    }\n"
            "\n"
            "    while (1) {\n"
            "        u16 pad = pad_read();\n"
            "\n"
            "        if (pad & PAD_LEFT)  yaw -= 2;\n"
            "        if (pad & PAD_RIGHT) yaw += 2;\n"
            "        if (pad & PAD_UP)    pitch -= 2;\n"
            "        if (pad & PAD_DOWN)  pitch += 2;\n"
            "        if (pad & PAD_A)     distance -= FIX(4);\n"
            "        if (pad & PAD_B)     distance += FIX(4);\n"
            "\n"
            "        if (distance < FIX(32))\n"
            "            distance = FIX(32);\n"
            "\n"
            "        /* Nothing on the pad turns it, so it turns by itself. */\n"
            "        if (!(pad & (PAD_LEFT | PAD_RIGHT)))\n"
            "            yaw += 1;\n"
            "\n"
            "        mesh3d_draw(&mesh, yaw & 255, pitch & 255, distance);\n"
            "        vdp_wait_vblank();\n"
            "    }\n"
            "\n"
            "    return 0;\n"
            "}\n",
            SegaSaturnExporter::CameraDistance());
    }
    else {
        snprintf(text, sizeof(text),
            "/* Generated by the Hatch Game Engine's SEGA Saturn exporter.\n"
            " *\n"
            " * The scene is in res/ as a palette and an eight-bit picture. VDP2 shows\n"
            " * a window of it as a bitmap background and the pad moves the window; the\n"
            " * game goes here.\n"
            " */\n"
            "\n"
            "#include \"saturn.h\"\n"
            "\n"
            "#define IMAGE_W %d\n"
            "#define IMAGE_H %d\n"
            "\n"
            "extern const unsigned char  scene_image[];\n"
            "extern const unsigned short scene_palette[];\n"
            "\n"
            "int main(void) {\n"
            "    int cameraX = 0, cameraY = 0;\n"
            "    int lastX = -1, lastY = -1;\n"
            "\n"
            "    vdp_init_bitmap();\n"
            "    vdp_load_palette(scene_palette, 256);\n"
            "    pad_init();\n"
            "\n"
            "    while (1) {\n"
            "        u16 pad = pad_read();\n"
            "\n"
            "        if (pad & PAD_RIGHT) cameraX += 4;\n"
            "        if (pad & PAD_LEFT)  cameraX -= 4;\n"
            "        if (pad & PAD_DOWN)  cameraY += 4;\n"
            "        if (pad & PAD_UP)    cameraY -= 4;\n"
            "\n"
            "        if (cameraX < 0) cameraX = 0;\n"
            "        if (cameraY < 0) cameraY = 0;\n"
            "        if (cameraX > IMAGE_W - SCREEN_W) cameraX = IMAGE_W - SCREEN_W;\n"
            "        if (cameraY > IMAGE_H - SCREEN_H) cameraY = IMAGE_H - SCREEN_H;\n"
            "        if (cameraX < 0) cameraX = 0;\n"
            "        if (cameraY < 0) cameraY = 0;\n"
            "\n"
            "        /* Copying a screenful costs enough that it is only worth doing\n"
            "         * when the camera has actually moved. */\n"
            "        if (cameraX != lastX || cameraY != lastY) {\n"
            "            vdp_blit(scene_image, IMAGE_W, IMAGE_H, cameraX, cameraY);\n"
            "            lastX = cameraX;\n"
            "            lastY = cameraY;\n"
            "        }\n"
            "\n"
            "        vdp_wait_vblank();\n"
            "    }\n"
            "\n"
            "    return 0;\n"
            "}\n",
            result->ImageWidth, result->ImageHeight);
    }

    snprintf(path, sizeof(path), "%s/src/main.c", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the art, pulled in by the assembler ---
    //
    // Every symbol is defined twice, once with a leading underscore and once
    // without. Whether a C symbol gets that underscore is a property of the
    // toolchain, not of the target -- sh-elf builds prefix, sh-linux ones do
    // not -- and an export that only guesses right on the compiler it was
    // tested against is an export that fails to link on somebody else's.
    if (result->Is3D) {
        StringUtils::Copy(text,
            "/* The scene's geometry, as the exporter wrote it. */\n"
            "\n"
            "    .section .rodata\n"
            "    .align 4\n"
            "    .global scene_mesh\n"
            "    .global _scene_mesh\n"
            "\n"
            "scene_mesh:\n"
            "_scene_mesh:\n"
            "    .incbin \"res/mesh.bin\"\n",
            sizeof(text));
    }
    else {
        StringUtils::Copy(text,
            "/* The scene's art, as the exporter wrote it. */\n"
            "\n"
            "    .section .rodata\n"
            "    .align 4\n"
            "    .global scene_palette\n"
            "    .global _scene_palette\n"
            "    .global scene_image\n"
            "    .global _scene_image\n"
            "\n"
            "scene_palette:\n"
            "_scene_palette:\n"
            "    .incbin \"res/palette.bin\"\n"
            "\n"
            "    .align 4\n"
            "scene_image:\n"
            "_scene_image:\n"
            "    .incbin \"res/image.bin\"\n",
            sizeof(text));
    }

    snprintf(path, sizeof(path), "%s/src/scene_data.s", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    // --- the Makefile ---
    //
    // Two binaries come out of this and they are not the same kind of thing.
    // 0.BIN is the program, linked to run from 0x06004000. IP.BIN is the disc
    // header, which lives outside the filesystem in the first sixteen sectors
    // and is put there by mkisofs -G. The program has to be the first file in
    // the root directory, which is why it is named with a digit.
    snprintf(text, sizeof(text),
        "# %s, for the SEGA Saturn.\n"
        "#\n"
        "# Needs an SH-2 cross compiler. The one from\n"
        "# https://github.com/SaturnSDK works, and so does any sh-elf-gcc:\n"
        "# nothing here uses SEGA's libraries.\n"
        "#\n"
        "#   make                 build cd/%s.iso\n"
        "#   make SH_PREFIX=...   if your toolchain is not on the PATH\n"
        "\n"
        "SH_PREFIX ?= sh-elf-\n"
        "\n"
        "CC      = $(SH_PREFIX)gcc\n"
        "OBJCOPY = $(SH_PREFIX)objcopy\n"
        "\n"
        "# -m2 is the SH-2, and big endian is its default -- the Saturn is not a\n"
        "# little endian machine and elf32-littlesh will not run on one.\n"
        "CFLAGS  = -m2 -O2 -Wall -fomit-frame-pointer -ffreestanding -nostdlib -Isrc\n"
        "LDFLAGS = -T src/saturn.ld -nostdlib\n"
        "\n"
        "OBJS = src/crt0.o src/main.o src/vdp.o src/pad.o src/string.o \\\n"
        "       src/scene3d.o src/scene_data.o\n"
        "\n"
        "ISO = cd/%s.iso\n"
        "\n"
        "all: $(ISO)\n"
        "\n"
        "%%.o: %%.c\n"
        "\t$(CC) $(CFLAGS) -c $< -o $@\n"
        "\n"
        "%%.o: %%.s\n"
        "\t$(CC) $(CFLAGS) -c $< -o $@\n"
        "\n"
        "# The data is pulled in with .incbin against paths relative to here, so\n"
        "# the assembler is run from here and told where to look.\n"
        "src/scene_data.o: src/scene_data.s $(wildcard res/*.bin)\n"
        "\t$(CC) $(CFLAGS) -Wa,-I,. -c $< -o $@\n"
        "\n"
        "src/ip.o: src/ip.s src/ip_header.inc\n"
        "\t$(CC) $(CFLAGS) -Wa,-I,src -c $< -o $@\n"
        "\n"
        "cd/0.BIN: $(OBJS)\n"
        "\tmkdir -p cd\n"
        "\t$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o out.elf -lgcc\n"
        "\t$(OBJCOPY) -O binary out.elf $@\n"
        "\n"
        "IP.BIN: src/ip.o\n"
        "\t$(OBJCOPY) -O binary --only-section=.ip $< $@\n"
        "\n"
        "$(ISO): cd/0.BIN IP.BIN\n"
        "\tgenisoimage -quiet -sysid \"SEGA SEGASATURN\" -volid \"%s\" \\\n"
        "\t  -G IP.BIN -full-iso9660-filenames -o $@ cd/0.BIN\n"
        "\n"
        "clean:\n"
        "\trm -rf cd out.elf IP.BIN src/*.o\n"
        "\n"
        ".PHONY: all clean\n",
        Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
        "scene", "scene", "HATCHSCENE");

    snprintf(path, sizeof(path), "%s/Makefile", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return SegaSaturnExporter::WriteReadme(outputPath, result);
}

PRIVATE STATIC bool SegaSaturnExporter::WriteReadme(const char* outputPath, SegaSaturnExportResult* result) {
    char path[1024];
    char text[8192];

    if (result->Is3D) {
        snprintf(text, sizeof(text),
            "# %s, for the SEGA Saturn\n"
            "\n"
            "Exported from the Hatch Game Engine. This is the 3D scene, drawn on the\n"
            "Saturn's VDP1 the way the Saturn's own games drew polygons.\n"
            "\n"
            "| File | What it holds |\n"
            "| --- | --- |\n"
            "| `res/mesh.bin` | %d vertices and %d faces, in world space, 16.16 fixed point |\n"
            "| `src/main.c` | the program that turns and draws it |\n"
            "| `src/scene3d.c` | the transform, the sort and the VDP1 command list |\n"
            "| `src/vdp.c` | VDP1 and VDP2 setup |\n"
            "| `src/ip.s` | the disc header the console boots from |\n"
            "\n"
            "%d model(s) came across.\n"
            "\n"
            "## Building\n"
            "\n"
            "```sh\n"
            "make                    # needs sh-elf-gcc and genisoimage\n"
            "make SH_PREFIX=/path/to/sh-elf-\n"
            "```\n"
            "\n"
            "`cd/scene.iso` is the result. It boots in Mednafen, Yabause or Kronos.\n"
            "\n"
            "## How it draws\n"
            "\n"
            "The SH-2 has no floating point unit and the Saturn has no depth buffer, so\n"
            "neither does this. Every vertex is transformed in 16.16 fixed point, every\n"
            "face is thrown away if it is turned away from the camera, and what is left\n"
            "is sorted back to front and drawn in that order.\n"
            "\n"
            "VDP1 draws quads, not triangles. A triangle is sent as a quad whose last\n"
            "two corners are the same point, which is what the Saturn always did.\n"
            "\n"
            "Faces are flat shaded. The colour is the model's vertex colours averaged\n"
            "over the face, or its material's diffuse colour when it has no vertex\n"
            "colours -- VDP1 can do gouraud, but only from a table in its own VRAM, and\n"
            "that is a thing to add rather than a thing to fake.\n"
            "\n"
            "## What this is and is not\n"
            "\n"
            "The pad turns the scene: left and right yaw it, up and down pitch it, A\n"
            "and B move the camera in and out. That is all it does. Hatch's game logic\n"
            "is bytecode for a VM that does not exist on an SH-2, so none of it came\n"
            "across. The geometry did.\n"
            "\n"
            "Nothing here is SEGA's. There is no SGL and no SBL: the VDP registers are\n"
            "written directly, which is why this builds with a stock sh-elf-gcc.\n"
            "\n"
            "The disc header has no security code -- that is a signed blob only SEGA\n"
            "can produce. Emulators boot this; a retail console will not.\n",
            Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
            result->VertexCount, result->FaceCount, result->ModelCount);
    }
    else {
        snprintf(text, sizeof(text),
            "# %s, for the SEGA Saturn\n"
            "\n"
            "Exported from the Hatch Game Engine. The scene layer is a VDP2 bitmap\n"
            "background: eight bits a pixel, its colours in the Saturn's colour RAM.\n"
            "\n"
            "| File | What it holds |\n"
            "| --- | --- |\n"
            "| `res/palette.bin` | 256 colour words, five bits a channel, red in the low bits |\n"
            "| `res/image.bin` | a %dx%d picture, one byte a pixel, %d bytes |\n"
            "| `src/main.c` | the program that shows it |\n"
            "| `src/vdp.c` | VDP1 and VDP2 setup |\n"
            "| `src/ip.s` | the disc header the console boots from |\n"
            "\n"
            "%d of the %d colour(s) the scene uses are in the palette. Index 0 is not\n"
            "one of them: a VDP2 background treats it as transparent, and what shows\n"
            "through it is the back screen, which the runtime sets to black.\n"
            "\n"
            "## Building\n"
            "\n"
            "```sh\n"
            "make                    # needs sh-elf-gcc and genisoimage\n"
            "make SH_PREFIX=/path/to/sh-elf-\n"
            "```\n"
            "\n"
            "`cd/scene.iso` is the result. It boots in Mednafen, Yabause or Kronos.\n"
            "\n"
            "## What this is and is not\n"
            "\n"
            "The pad scrolls around the picture. That is all it does: Hatch's game\n"
            "logic is bytecode for a VM that does not exist on an SH-2, so none of it\n"
            "came across. The art did, at fifteen bits of colour.\n"
            "\n"
            "The picture is bigger than the bitmap VDP2 holds, so the SH-2 copies the\n"
            "window the camera is over rather than scrolling to it -- and only when the\n"
            "camera has moved.\n"
            "\n"
            "Nothing here is SEGA's. There is no SGL and no SBL: the VDP registers are\n"
            "written directly, which is why this builds with a stock sh-elf-gcc.\n"
            "\n"
            "The disc header has no security code -- that is a signed blob only SEGA\n"
            "can produce. Emulators boot this; a retail console will not.\n",
            Scene::CurrentScene[0] ? Scene::CurrentScene : "Scene",
            result->ImageWidth, result->ImageHeight, (int)result->ImageBytes,
            result->PaletteCount, result->ColorsFound);
    }

    snprintf(path, sizeof(path), "%s/README.md", outputPath);
    if (!SegaSaturnExporter::WriteText(path, text)) {
        snprintf(result->Message, sizeof(result->Message), "Could not write \"%s\".", path);
        return false;
    }

    return true;
}
