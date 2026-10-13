#ifndef ENGINE_RESOURCETYPES_SCENEFORMATS_SCENE3DFORMAT_H
#define ENGINE_RESOURCETYPES_SCENEFORMATS_SCENE3DFORMAT_H

class Scene3DObject;
class Scene3DSettings;
class XMLNode;

#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/TextFormats/XML/XMLParser.h>

class Scene3DFormat {
private:
    static float Attribute(XMLNode* node, const char* name, float fallback);
    static void AttributeString(XMLNode* node, const char* name, char* out, size_t outSize);
    static void ReadColor(XMLNode* node, float* r, float* g, float* b);

public:
    static bool Read(const char* path, Scene3DSettings* settings, vector<Scene3DObject>* objects);
    static bool Write(const char* path, Scene3DSettings* settings, vector<Scene3DObject>* objects);
};

#endif /* ENGINE_RESOURCETYPES_SCENEFORMATS_SCENE3DFORMAT_H */
