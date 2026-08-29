/* A 3D scene, drawn on VDP1.
 *
 * The Saturn has no floating point and no depth buffer. So everything here is
 * 16.16 fixed point, and the faces are sorted back to front and drawn in that
 * order -- the painter's algorithm, which is what the Saturn's own games did.
 *
 * The mesh comes out of the exporter already in world space, so the console
 * never has to know where a model sat in the scene: it rotates the whole thing
 * about the origin, projects it, throws away the faces turned away from the
 * camera, and hands what is left to VDP1 as polygon commands.
 */

#include "saturn.h"

/* A quarter turn of sine in 16.16, sampled every 64th of a circle. The other
 * three quarters are this one read backwards or negated. */
static const s32 sine_quarter[65] = {
        0,  1608,  3216,  4821,  6424,  8022,  9616, 11204,
    12785, 14359, 15924, 17479, 19024, 20557, 22078, 23586,
    25080, 26558, 28020, 29466, 30893, 32303, 33692, 35062,
    36410, 37736, 39040, 40320, 41576, 42806, 44011, 45190,
    46341, 47464, 48559, 49624, 50660, 51665, 52639, 53581,
    54491, 55368, 56212, 57022, 57798, 58538, 59244, 59914,
    60547, 61145, 61705, 62228, 62714, 63162, 63572, 63944,
    64277, 64571, 64827, 65043, 65220, 65358, 65457, 65516,
    65536
};

fixed fsin(int angle) {
    angle &= 255;
    if (angle < 64)  return  sine_quarter[angle];
    if (angle < 128) return  sine_quarter[128 - angle];
    if (angle < 192) return -sine_quarter[angle - 128];
    return -sine_quarter[256 - angle];
}

fixed fcos(int angle) {
    return fsin(angle + 64);
}

static s32  ScreenX[MESH3D_MAX_VERTICES];
static s32  ScreenY[MESH3D_MAX_VERTICES];
static s32  ViewZ[MESH3D_MAX_VERTICES];

/* Faces, and the depth each was found at, sorted into drawing order. */
static s16  Order[MESH3D_MAX_FACES];
static s32  Depth[MESH3D_MAX_FACES];
static int  Visible;

static u32 read32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u16 read16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}

int mesh3d_open(Mesh3D* mesh, const u8* blob) {
    if (read32(blob) != 0x48534D31) /* 'HSM1' */
        return 0;

    mesh->VertexCount = (int)read32(blob + 4);
    mesh->FaceCount = (int)read32(blob + 8);
    mesh->Vertices = blob + 12;
    mesh->Faces = blob + 12 + mesh->VertexCount * 12;

    if (mesh->VertexCount > MESH3D_MAX_VERTICES)
        mesh->VertexCount = MESH3D_MAX_VERTICES;
    if (mesh->FaceCount > MESH3D_MAX_FACES)
        mesh->FaceCount = MESH3D_MAX_FACES;

    return 1;
}

/* Back to front, so what is nearer is drawn over what is further.
 *
 * An insertion sort looks like the wrong choice until you notice what it is
 * sorting: a face list whose order barely changes from one frame to the next,
 * which is the case insertion sort is fastest on. */
static void sort_faces(void) {
    int i, j;

    for (i = 1; i < Visible; i++) {
        s32 key = Depth[i];
        s16 face = Order[i];

        for (j = i - 1; j >= 0 && Depth[j] < key; j--) {
            Depth[j + 1] = Depth[j];
            Order[j + 1] = Order[j];
        }

        Depth[j + 1] = key;
        Order[j + 1] = face;
    }
}

void mesh3d_draw(const Mesh3D* mesh, int yaw, int pitch, s32 distance) {
    fixed sy = fsin(yaw), cy = fcos(yaw);
    fixed sx = fsin(pitch), cx = fcos(pitch);
    int i;

    for (i = 0; i < mesh->VertexCount; i++) {
        const u8* v = mesh->Vertices + i * 12;
        fixed x = (fixed)read32(v);
        fixed y = (fixed)read32(v + 4);
        fixed z = (fixed)read32(v + 8);
        fixed rx, ry, rz, tz;

        rx = FMUL(x, cy) + FMUL(z, sy);
        rz = FMUL(z, cy) - FMUL(x, sy);
        ry = FMUL(y, cx) - FMUL(rz, sx);
        rz = FMUL(rz, cx) + FMUL(y, sx);

        tz = rz + distance;

        /* Nothing may sit at or behind the eye: the divide below would either
         * blow up or turn the picture inside out. */
        if (tz < MESH3D_NEAR)
            tz = MESH3D_NEAR;

        ScreenX[i] = (s32)(((s64)rx * MESH3D_FOCAL / tz) >> 16);
        ScreenY[i] = (s32)(((s64)ry * MESH3D_FOCAL / tz) >> 16);
        ViewZ[i] = tz;
    }

    Visible = 0;

    for (i = 0; i < mesh->FaceCount; i++) {
        const u8* f = mesh->Faces + i * 12;
        int a = read16(f), b = read16(f + 2), c = read16(f + 4);
        s32 cross;

        if (a >= mesh->VertexCount || b >= mesh->VertexCount || c >= mesh->VertexCount)
            continue;

        /* Which way a face is turned, once it is flat on the screen: the sign
         * of the cross product of two of its edges.
         *
         * Negative is the front. Screen Y runs downwards on VDP1, which flips
         * the sign against the usual maths-class answer -- and a closed convex
         * model looks solid either way, since culling the wrong half just shows
         * you the inside of the far side with the same silhouette. Counting the
         * faces a cube keeps is what settles it: four at an edge-on view, two
         * face-on, and that is this sign. */
        cross = (ScreenX[b] - ScreenX[a]) * (ScreenY[c] - ScreenY[a]) -
                (ScreenY[b] - ScreenY[a]) * (ScreenX[c] - ScreenX[a]);

        if (cross >= 0)
            continue;

        if (Visible >= MESH3D_MAX_FACES)
            break;

        Order[Visible] = (s16)i;
        Depth[Visible] = ViewZ[a] + ViewZ[b] + ViewZ[c];
        Visible++;
    }

    sort_faces();

    vdp1_begin();

    for (i = 0; i < Visible; i++) {
        const u8* f = mesh->Faces + Order[i] * 12;
        int a = read16(f), b = read16(f + 2), c = read16(f + 4), d = read16(f + 6);
        u16 colour = read16(f + 8);

        if (d >= mesh->VertexCount)
            d = c;

        vdp1_quad((s16)ScreenX[a], (s16)ScreenY[a],
                  (s16)ScreenX[b], (s16)ScreenY[b],
                  (s16)ScreenX[c], (s16)ScreenY[c],
                  (s16)ScreenX[d], (s16)ScreenY[d], colour);
    }

    vdp1_end();
}

int mesh3d_visible_faces(void) {
    return Visible;
}
