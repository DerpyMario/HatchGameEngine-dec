/* What a Hatch scene needs from a Dreamcast.
 *
 * Unlike the older SEGA exports, this one does not talk to the hardware
 * directly: KallistiOS is an operating system and a driver set, and rewriting
 * either would be worse than using them. What is here is the part above that --
 * turning an exported scene into PowerVR polygons.
 *
 * The PVR is a tile-based deferred renderer. Opaque polygons are sorted per
 * pixel in hardware, so unlike the Saturn there is no depth sort to do and no
 * painter's algorithm to get wrong: geometry can be submitted in any order and
 * comes out right.
 */

#ifndef HATCH_DREAMCAST_H
#define HATCH_DREAMCAST_H

#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* 640x480, which is what pvr_init_defaults sets up. */
#define SCREEN_W 640
#define SCREEN_H 480

/* ------------------------------------------------------------ 2D scene --- */

/* A scene layer, cut into square textures because the PVR wants powers of two
 * and will not take a texture larger than 1024 on a side. */
typedef struct {
    int      Width, Height;     /* the whole layer, in pixels */
    int      TileSize;          /* one texture, in pixels */
    int      Columns, Rows;
    pvr_ptr_t* Textures;        /* Columns * Rows of them, in video memory */
} Scene2D;

int  scene2d_load(Scene2D* scene, const char* path);
void scene2d_draw(const Scene2D* scene, float cameraX, float cameraY);
void scene2d_free(Scene2D* scene);

/* ------------------------------------------------------------ 3D scene --- */

typedef struct { float X, Y, Z; } Vec3;

typedef struct {
    uint16_t A, B, C, D;
    uint32_t Color;
    uint16_t Flags;
} Face3D;

#define FACE_TRIANGLE 0x0001

typedef struct {
    Vec3*    Vertices;
    Face3D*  Faces;
    int      VertexCount;
    int      FaceCount;
    float    Radius;            /* how far the geometry reaches from the origin */
} Mesh3D;

int  mesh3d_load(Mesh3D* mesh, const char* path);
void mesh3d_draw(const Mesh3D* mesh, float yaw, float pitch, float distance);
void mesh3d_free(Mesh3D* mesh);

/* --------------------------------------------------------------- input --- */

uint32_t pad_read(void);

#endif /* HATCH_DREAMCAST_H */
