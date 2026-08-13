#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/TextFormats/XML/XMLParser.h>

need_t Scene3DObject;
need_t Scene3DSettings;
need_t XMLNode;

class Scene3DFormat {
};
#endif

#include <Engine/ResourceTypes/SceneFormats/Scene3DFormat.h>

#include <Engine/Diagnostics/Log.h>
#include <Engine/Filesystem/File.h>
#include <Engine/Rendering/Scene3DTypes.h>
#include <Engine/TextFormats/XML/XMLParser.h>

// The file a 3D scene is kept in.
//
// Tile scenes have Tiled, and a format that came with it. A 3D scene had
// neither: the engine could draw models, light them and fog them, but only ever
// because a script said so at runtime, so there was nothing to save and nothing
// to open.
//
// This is that file. It is XML, like the rest of what a Hatch project keeps its
// configuration in, and it holds what the engine actually needs to put the
// scene back: the camera, the lighting, the fog, and every model with where it
// sits. The models themselves stay where they are and are named by path, the
// same way a tile scene names its tilesets.

PRIVATE STATIC float Scene3DFormat::Attribute(XMLNode* node, const char* name, float fallback) {
    if (!node->attributes.Exists(name))
        return fallback;

    return (float)XMLParser::TokenToNumber(node->attributes.Get(name));
}

PRIVATE STATIC void Scene3DFormat::AttributeString(XMLNode* node, const char* name, char* out, size_t outSize) {
    out[0] = '\0';

    if (!node->attributes.Exists(name))
        return;

    Token token = node->attributes.Get(name);
    size_t length = token.Length;
    if (length >= outSize)
        length = outSize - 1;

    memcpy(out, token.Start, length);
    out[length] = '\0';
}

PRIVATE STATIC void Scene3DFormat::ReadColor(XMLNode* node, float* r, float* g, float* b) {
    *r = Scene3DFormat::Attribute(node, "r", *r);
    *g = Scene3DFormat::Attribute(node, "g", *g);
    *b = Scene3DFormat::Attribute(node, "b", *b);
}

PUBLIC STATIC bool Scene3DFormat::Read(const char* path, Scene3DSettings* settings, vector<Scene3DObject>* objects) {
    XMLNode* xml = XMLParser::ParseFromResource(path);
    if (!xml) {
        Log::Print(Log::LOG_ERROR, "Could not read the 3D scene \"%s\"!", path);
        return false;
    }

    if (!xml->children.size()) {
        XMLParser::Free(xml);
        Log::Print(Log::LOG_ERROR, "\"%s\" is empty.", path);
        return false;
    }

    XMLNode* scene = xml->children[0];
    if (!XMLParser::MatchToken(scene->name, "scene3d")) {
        XMLParser::Free(xml);
        Log::Print(Log::LOG_ERROR, "\"%s\" is not a 3D scene.", path);
        return false;
    }

    objects->clear();

    for (size_t i = 0; i < scene->children.size(); i++) {
        XMLNode* node = scene->children[i];

        if (XMLParser::MatchToken(node->name, "camera")) {
            settings->FOV = Scene3DFormat::Attribute(node, "fov", settings->FOV);
            settings->NearClippingPlane = Scene3DFormat::Attribute(node, "near", settings->NearClippingPlane);
            settings->FarClippingPlane = Scene3DFormat::Attribute(node, "far", settings->FarClippingPlane);
            settings->CameraYaw = Scene3DFormat::Attribute(node, "yaw", settings->CameraYaw);
            settings->CameraPitch = Scene3DFormat::Attribute(node, "pitch", settings->CameraPitch);
            settings->CameraDistance = Scene3DFormat::Attribute(node, "distance", settings->CameraDistance);
            settings->TargetX = Scene3DFormat::Attribute(node, "targetX", settings->TargetX);
            settings->TargetY = Scene3DFormat::Attribute(node, "targetY", settings->TargetY);
            settings->TargetZ = Scene3DFormat::Attribute(node, "targetZ", settings->TargetZ);
        }
        else if (XMLParser::MatchToken(node->name, "lighting")) {
            for (size_t e = 0; e < node->children.size(); e++) {
                XMLNode* light = node->children[e];

                if (XMLParser::MatchToken(light->name, "ambient"))
                    Scene3DFormat::ReadColor(light, &settings->AmbientR, &settings->AmbientG, &settings->AmbientB);
                else if (XMLParser::MatchToken(light->name, "diffuse"))
                    Scene3DFormat::ReadColor(light, &settings->DiffuseR, &settings->DiffuseG, &settings->DiffuseB);
                else if (XMLParser::MatchToken(light->name, "specular"))
                    Scene3DFormat::ReadColor(light, &settings->SpecularR, &settings->SpecularG, &settings->SpecularB);
            }
        }
        else if (XMLParser::MatchToken(node->name, "fog")) {
            settings->FogEquation = (int)Scene3DFormat::Attribute(node, "equation", (float)settings->FogEquation);
            settings->FogStart = Scene3DFormat::Attribute(node, "start", settings->FogStart);
            settings->FogEnd = Scene3DFormat::Attribute(node, "end", settings->FogEnd);
            settings->FogDensity = Scene3DFormat::Attribute(node, "density", settings->FogDensity);
            settings->FogSmoothness = Scene3DFormat::Attribute(node, "smoothness", settings->FogSmoothness);
            Scene3DFormat::ReadColor(node, &settings->FogR, &settings->FogG, &settings->FogB);
        }
        else if (XMLParser::MatchToken(node->name, "model")) {
            Scene3DObject object;

            Scene3DFormat::AttributeString(node, "source", object.Source, sizeof(object.Source));
            if (!object.Source[0])
                continue;

            object.X = Scene3DFormat::Attribute(node, "x", 0.0f);
            object.Y = Scene3DFormat::Attribute(node, "y", 0.0f);
            object.Z = Scene3DFormat::Attribute(node, "z", 0.0f);
            object.RotationX = Scene3DFormat::Attribute(node, "rotationX", 0.0f);
            object.RotationY = Scene3DFormat::Attribute(node, "rotationY", 0.0f);
            object.RotationZ = Scene3DFormat::Attribute(node, "rotationZ", 0.0f);
            object.ScaleX = Scene3DFormat::Attribute(node, "scaleX", 1.0f);
            object.ScaleY = Scene3DFormat::Attribute(node, "scaleY", 1.0f);
            object.ScaleZ = Scene3DFormat::Attribute(node, "scaleZ", 1.0f);

            objects->push_back(object);
        }
    }

    XMLParser::Free(xml);

    Log::Print(Log::LOG_IMPORTANT, "Read the 3D scene \"%s\": %d model(s).", path, (int)objects->size());

    return true;
}

PUBLIC STATIC bool Scene3DFormat::Write(const char* path, Scene3DSettings* settings, vector<Scene3DObject>* objects) {
    char line[1024];
    std::string text;

    text += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    text += "<scene3d version=\"1\">\n";

    snprintf(line, sizeof(line),
        " <camera fov=\"%g\" near=\"%g\" far=\"%g\" yaw=\"%g\" pitch=\"%g\" distance=\"%g\""
        " targetX=\"%g\" targetY=\"%g\" targetZ=\"%g\"/>\n",
        settings->FOV, settings->NearClippingPlane, settings->FarClippingPlane,
        settings->CameraYaw, settings->CameraPitch, settings->CameraDistance,
        settings->TargetX, settings->TargetY, settings->TargetZ);
    text += line;

    text += " <lighting>\n";
    snprintf(line, sizeof(line), "  <ambient r=\"%g\" g=\"%g\" b=\"%g\"/>\n",
        settings->AmbientR, settings->AmbientG, settings->AmbientB);
    text += line;
    snprintf(line, sizeof(line), "  <diffuse r=\"%g\" g=\"%g\" b=\"%g\"/>\n",
        settings->DiffuseR, settings->DiffuseG, settings->DiffuseB);
    text += line;
    snprintf(line, sizeof(line), "  <specular r=\"%g\" g=\"%g\" b=\"%g\"/>\n",
        settings->SpecularR, settings->SpecularG, settings->SpecularB);
    text += line;
    text += " </lighting>\n";

    snprintf(line, sizeof(line),
        " <fog equation=\"%d\" start=\"%g\" end=\"%g\" density=\"%g\" smoothness=\"%g\""
        " r=\"%g\" g=\"%g\" b=\"%g\"/>\n",
        settings->FogEquation, settings->FogStart, settings->FogEnd,
        settings->FogDensity, settings->FogSmoothness,
        settings->FogR, settings->FogG, settings->FogB);
    text += line;

    for (size_t i = 0; i < objects->size(); i++) {
        Scene3DObject& object = (*objects)[i];

        snprintf(line, sizeof(line),
            " <model source=\"%s\" x=\"%g\" y=\"%g\" z=\"%g\""
            " rotationX=\"%g\" rotationY=\"%g\" rotationZ=\"%g\""
            " scaleX=\"%g\" scaleY=\"%g\" scaleZ=\"%g\"/>\n",
            object.Source, object.X, object.Y, object.Z,
            object.RotationX, object.RotationY, object.RotationZ,
            object.ScaleX, object.ScaleY, object.ScaleZ);
        text += line;
    }

    text += "</scene3d>\n";

    if (!File::WriteAllBytes(path, text.c_str(), text.size())) {
        Log::Print(Log::LOG_ERROR, "Could not write \"%s\"!", path);
        return false;
    }

    Log::Print(Log::LOG_IMPORTANT, "Saved the 3D scene \"%s\": %d model(s).", path, (int)objects->size());

    return true;
}
