/* A 3D scene, on the PowerVR.
 *
 * The contrast with the Saturn export is the point of this target. There the
 * SH-2 had no floating point, so everything was 16.16 fixed point; and the
 * hardware had no depth buffer, so every face had to be sorted back to front
 * every frame and drawn in that order.
 *
 * Here the SH-4 has an FPU, so the transform is ordinary floats. And the PVR is
 * a tile-based deferred renderer that resolves opaque geometry per pixel in
 * hardware, so there is no sort at all -- faces go out in whatever order they
 * are stored and come back correct.
 */

#include "dreamcast.h"

static uint32_t read32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static float readfloat(const uint8_t* p) {
    union { uint32_t i; float f; } value;
    value.i = read32(p);
    return value.f;
}

int mesh3d_load(Mesh3D* mesh, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("mesh3d: could not open %s\n", path);
        return 0;
    }

    uint8_t header[12];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }

    if (read32(header) != 0x33434448) { /* 'HDC3' */
        printf("mesh3d: %s is not a Dreamcast mesh\n", path);
        fclose(file);
        return 0;
    }

    mesh->VertexCount = (int)read32(header + 4);
    mesh->FaceCount = (int)read32(header + 8);

    mesh->Vertices = malloc(sizeof(Vec3) * mesh->VertexCount);
    mesh->Faces = malloc(sizeof(Face3D) * mesh->FaceCount);

    if (!mesh->Vertices || !mesh->Faces) {
        mesh3d_free(mesh);
        fclose(file);
        return 0;
    }

    mesh->Radius = 0.0f;

    for (int i = 0; i < mesh->VertexCount; i++) {
        uint8_t raw[12];
        if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
            mesh3d_free(mesh);
            fclose(file);
            return 0;
        }

        mesh->Vertices[i].X = readfloat(raw);
        mesh->Vertices[i].Y = readfloat(raw + 4);
        mesh->Vertices[i].Z = readfloat(raw + 8);

        float x = mesh->Vertices[i].X;
        float y = mesh->Vertices[i].Y;
        float z = mesh->Vertices[i].Z;
        float reach = sqrtf(x * x + y * y + z * z);

        if (reach > mesh->Radius)
            mesh->Radius = reach;
    }

    for (int i = 0; i < mesh->FaceCount; i++) {
        uint8_t raw[16];
        if (fread(raw, 1, sizeof(raw), file) != sizeof(raw)) {
            mesh3d_free(mesh);
            fclose(file);
            return 0;
        }

        mesh->Faces[i].A = read16(raw);
        mesh->Faces[i].B = read16(raw + 2);
        mesh->Faces[i].C = read16(raw + 4);
        mesh->Faces[i].D = read16(raw + 6);
        mesh->Faces[i].Color = read32(raw + 8);
        mesh->Faces[i].Flags = read16(raw + 12);
    }

    fclose(file);

    printf("mesh3d: %d vertices, %d faces, reaching %.1f from the origin\n",
        mesh->VertexCount, mesh->FaceCount, mesh->Radius);

    return 1;
}

/* Where the camera puts a point on screen, or nothing if it is behind the eye. */
static int project(const Vec3* in, float sinYaw, float cosYaw, float sinPitch,
                   float cosPitch, float distance, float* outX, float* outY, float* outZ) {
    float x = in->X * cosYaw + in->Z * sinYaw;
    float z = in->Z * cosYaw - in->X * sinYaw;
    float y = in->Y * cosPitch - z * sinPitch;

    z = z * cosPitch + in->Y * sinPitch;
    z += distance;

    /* Anything at or behind the eye has no place on the screen, and dividing by
     * it would wrap the picture inside out. */
    if (z < 1.0f)
        return 0;

    const float focal = 480.0f;

    *outX = SCREEN_W * 0.5f + (x * focal) / z;
    *outY = SCREEN_H * 0.5f + (y * focal) / z;

    /* The PVR wants 1/w rather than a depth, with larger meaning nearer. */
    *outZ = 1.0f / z;

    return 1;
}

void mesh3d_draw(const Mesh3D* mesh, float yaw, float pitch, float distance) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_vertex_t vertex;

    float sinYaw = sinf(yaw), cosYaw = cosf(yaw);
    float sinPitch = sinf(pitch), cosPitch = cosf(pitch);

    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);

    /* The PVR culls counter-clockwise faces by default, and the cull below
     * already drops the ones facing away. Leaving both on removes every face
     * there is and the screen comes up black -- which is exactly what it did. */
    context.gen.culling = PVR_CULLING_NONE;

    pvr_poly_compile(&header, &context);
    pvr_prim(&header, sizeof(header));

    for (int i = 0; i < mesh->FaceCount; i++) {
        const Face3D* face = &mesh->Faces[i];

        uint16_t corners[4] = { face->A, face->B, face->C, face->D };
        float x[4], y[4], z[4];
        int ok = 1;

        int count = (face->Flags & FACE_TRIANGLE) ? 3 : 4;

        for (int c = 0; c < count; c++) {
            if (corners[c] >= mesh->VertexCount ||
                !project(&mesh->Vertices[corners[c]], sinYaw, cosYaw,
                         sinPitch, cosPitch, distance, &x[c], &y[c], &z[c])) {
                ok = 0;
                break;
            }
        }

        if (!ok)
            continue;

        /* Backfaces are dropped here rather than left to the hardware: the PVR
         * has a culling mode, but it works on the winding the TA sees, and the
         * exporter's winding is the engine's rather than the PVR's. */
        float cross = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
        if (cross >= 0.0f)
            continue;

        vertex.argb = face->Color;
        vertex.oargb = 0;
        vertex.u = vertex.v = 0.0f;

        /* A strip, so the corners go out in the order the PVR reads them:
         * A, B, then C -- and for a quad, D last. */
        vertex.flags = PVR_CMD_VERTEX;
        vertex.x = x[0]; vertex.y = y[0]; vertex.z = z[0];
        pvr_prim(&vertex, sizeof(vertex));

        vertex.x = x[1]; vertex.y = y[1]; vertex.z = z[1];
        pvr_prim(&vertex, sizeof(vertex));

        if (count == 4) {
            vertex.x = x[3]; vertex.y = y[3]; vertex.z = z[3];
            pvr_prim(&vertex, sizeof(vertex));

            vertex.flags = PVR_CMD_VERTEX_EOL;
            vertex.x = x[2]; vertex.y = y[2]; vertex.z = z[2];
            pvr_prim(&vertex, sizeof(vertex));
        }
        else {
            vertex.flags = PVR_CMD_VERTEX_EOL;
            vertex.x = x[2]; vertex.y = y[2]; vertex.z = z[2];
            pvr_prim(&vertex, sizeof(vertex));
        }
    }
}

void mesh3d_free(Mesh3D* mesh) {
    free(mesh->Vertices);
    free(mesh->Faces);
    mesh->Vertices = NULL;
    mesh->Faces = NULL;
}
