#ifndef ENGINE_EXPORTERS_SEGASATURNEXPORTER_H
#define ENGINE_EXPORTERS_SEGASATURNEXPORTER_H

class TileLayer;
class Scene3DObject;
class Scene3DSettings;
class Mesh;
class IModel;

#include <Engine/Includes/Standard.h>
#include <Engine/Exporters/SegaSaturnTypes.h>
#include <Engine/Rendering/Scene3DTypes.h>

class SegaSaturnExporter {
private:
    static int CameraDistance();
    static Uint32 QuantizeColor(Uint32 argb);
    static long ColorDistance(Uint32 a, Uint32 b);
    static int NearestInPalette(Uint32 color);
    static void GatherColors(TileLayer* layer, int width, int height, vector<SaturnColorUse>* out);
    static void BuildImage(TileLayer* layer, int width, int height);
    static bool WriteBinary(const char* path, const void* data, size_t size);
    static bool WriteText(const char* path, const char* text);
    static bool CopyFile(const char* from, const char* to);
    static bool FindRuntime(char* out, size_t outSize);
    static void HeaderField(char* out, size_t width, const char* text);
    static Uint16 ColorToSaturn(Uint32 argb);
    static Uint16 FaceColor(Mesh* mesh, IModel* model, Sint32* corners, int cornerCount);
    static void CollectModel(IModel* model, Scene3DObject* object);
    static bool WriteProject(const char* outputPath, SegaSaturnExportResult* result);
    static bool WriteIPHeader(const char* outputPath, SegaSaturnExportResult* result);
    static bool WriteSources(const char* outputPath, SegaSaturnExportResult* result);
    static bool WriteReadme(const char* outputPath, SegaSaturnExportResult* result);

public:
    static SegaSaturnExportResult ExportScene3D(const char* outputPath, const char* scenePath);
    static SegaSaturnExportResult ExportScene(const char* outputPath);
};

#endif /* ENGINE_EXPORTERS_SEGASATURNEXPORTER_H */
