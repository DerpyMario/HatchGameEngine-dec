#ifndef SCENE3DTYPES_H
#define SCENE3DTYPES_H

#include <Engine/Includes/Standard.h>

// One model placed in a 3D scene. The model itself is loaded when the scene is,
// and the scene file only carries where it came from and where it sits.
struct Scene3DObject {
    char  Source[512];

    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    float RotationX = 0.0f, RotationY = 0.0f, RotationZ = 0.0f;
    float ScaleX = 1.0f, ScaleY = 1.0f, ScaleZ = 1.0f;

    void* Model = nullptr;
    bool  Failed = false;

    Scene3DObject() { Source[0] = '\0'; }
};

// Everything about a 3D scene that is not one of its models: where the camera
// is looking from, and how the world is lit.
struct Scene3DSettings {
    float FOV = 70.0f;
    float NearClippingPlane = 1.0f;
    float FarClippingPlane = 32768.0f;

    // The camera orbits a point, which is how a scene is looked at while it is
    // being put together rather than how a game would move through it.
    float CameraYaw = 0.7f;
    float CameraPitch = 0.5f;
    float CameraDistance = 240.0f;
    float TargetX = 0.0f, TargetY = 0.0f, TargetZ = 0.0f;

    float AmbientR = 1.0f, AmbientG = 1.0f, AmbientB = 1.0f;
    float DiffuseR = 1.0f, DiffuseG = 1.0f, DiffuseB = 1.0f;
    float SpecularR = 1.0f, SpecularG = 1.0f, SpecularB = 1.0f;

    int   FogEquation = 0;
    float FogStart = 0.0f;
    float FogEnd = 1.0f;
    float FogDensity = 1.0f;
    float FogSmoothness = 1.0f;
    float FogR = 0.0f, FogG = 0.0f, FogB = 0.0f;
};

#endif /* SCENE3DTYPES_H */
