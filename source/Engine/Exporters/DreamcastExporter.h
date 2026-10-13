#ifndef ENGINE_EXPORTERS_DREAMCASTEXPORTER_H
#define ENGINE_EXPORTERS_DREAMCASTEXPORTER_H

class TileLayer;
class Scene3DObject;
class Scene3DSettings;
class Mesh;
class IModel;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/DreamcastTypes.h>
#include <Engine/Rendering/Scene3DTypes.h>

class DreamcastExporter {
private:
    static Uint16 ToRGB565(Uint32 argb);
    static void BuildTextures(TileLayer* layer, int width, int height);
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool CopyFile(const char* from, const char* to);
    static bool FindRuntime(char* out, size_t outSize);
    static Uint32 FaceColor(Mesh* mesh, IModel* model, Sint32* corners, int cornerCount);
    static void CollectModel(IModel* model, Scene3DObject* object);
    static int CameraDistance();
    static void PushU32(vector<Uint8>* out, Uint32 value);
    static void PushU16(vector<Uint8>* out, Uint16 value);
    static void PushFloat(vector<Uint8>* out, float value);
    static bool WriteProject(const char* outputPath, DreamcastExportResult* result);
    static bool WriteSources(const char* outputPath, DreamcastExportResult* result);
    static bool WriteReadme(const char* outputPath, DreamcastExportResult* result);

public:
    static DreamcastExportResult ExportScene3D(const char* outputPath, const char* scenePath);
    static DreamcastExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_DREAMCASTEXPORTER_H */
