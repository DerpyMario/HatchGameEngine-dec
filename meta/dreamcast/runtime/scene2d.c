/* A scene layer, on screen.
 *
 * The layer comes over as a grid of square RGB565 textures. The PVR will not
 * take a texture bigger than 1024 on a side and wants powers of two, and a
 * scene is neither, so the exporter cuts it up and this puts it back together
 * as a row of textured quads.
 *
 * Only the tiles the camera can see are submitted. That is not an optimisation
 * for its own sake: a large scene is more texture than will fit on screen, and
 * submitting all of it would spend the whole frame on polygons nobody sees.
 */

#include "dreamcast.h"

static uint32_t read32(const uint8_t* p) {
    /* The SH-4 is little endian here, unlike every other SEGA machine this
     * engine exports to, so the file is written that way round. */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int scene2d_load(Scene2D* scene, const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        printf("scene2d: could not open %s\n", path);
        return 0;
    }

    uint8_t header[24];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return 0;
    }

    if (read32(header) != 0x32434448) { /* 'HDC2' */
        printf("scene2d: %s is not a Dreamcast scene\n", path);
        fclose(file);
        return 0;
    }

    scene->Width    = (int)read32(header + 4);
    scene->Height   = (int)read32(header + 8);
    scene->TileSize = (int)read32(header + 12);
    scene->Columns  = (int)read32(header + 16);
    scene->Rows     = (int)read32(header + 20);

    int count = scene->Columns * scene->Rows;
    size_t bytes = (size_t)scene->TileSize * scene->TileSize * 2;

    scene->Textures = malloc(sizeof(pvr_ptr_t) * count);
    if (!scene->Textures) {
        fclose(file);
        return 0;
    }

    /* Textures go into video memory, and the copy runs through a staging
     * buffer in main memory because reading a file straight into VRAM is not
     * something the PVR allows. */
    void* staging = malloc(bytes);
    if (!staging) {
        free(scene->Textures);
        scene->Textures = NULL;
        fclose(file);
        return 0;
    }

    for (int i = 0; i < count; i++) {
        scene->Textures[i] = pvr_mem_malloc(bytes);

        if (!scene->Textures[i] || fread(staging, 1, bytes, file) != bytes) {
            printf("scene2d: ran out of video memory at tile %d of %d\n", i, count);

            /* Whatever loaded stays loaded; the rest are left null and skipped
             * when drawing, so a scene too big for the machine comes up partly
             * rather than not at all. */
            for (int j = i; j < count; j++)
                scene->Textures[j] = NULL;
            break;
        }

        pvr_txr_load(staging, scene->Textures[i], bytes);
    }

    free(staging);
    fclose(file);

    printf("scene2d: %dx%d in %dx%d tiles of %d\n",
        scene->Width, scene->Height, scene->Columns, scene->Rows, scene->TileSize);

    return 1;
}

void scene2d_draw(const Scene2D* scene, float cameraX, float cameraY) {
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    pvr_vertex_t vertex;

    int size = scene->TileSize;

    /* Which tiles the screen overlaps. */
    int firstColumn = (int)(cameraX / size);
    int firstRow = (int)(cameraY / size);
    int lastColumn = (int)((cameraX + SCREEN_W) / size);
    int lastRow = (int)((cameraY + SCREEN_H) / size);

    if (firstColumn < 0) firstColumn = 0;
    if (firstRow < 0) firstRow = 0;
    if (lastColumn >= scene->Columns) lastColumn = scene->Columns - 1;
    if (lastRow >= scene->Rows) lastRow = scene->Rows - 1;

    for (int row = firstRow; row <= lastRow; row++) {
        for (int column = firstColumn; column <= lastColumn; column++) {
            pvr_ptr_t texture = scene->Textures[column + row * scene->Columns];
            if (!texture)
                continue;

            pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                size, size, texture, PVR_FILTER_NONE);

            /* These quads face the screen by construction, so the hardware's
             * backface culling has nothing useful to say about them -- and if
             * it disagrees with the order the corners go out in, it removes
             * the whole background. */
            context.gen.culling = PVR_CULLING_NONE;

            pvr_poly_compile(&header, &context);
            pvr_prim(&header, sizeof(header));

            float left = column * size - cameraX;
            float top = row * size - cameraY;
            float right = left + size;
            float bottom = top + size;

            /* One quad, as a strip of four vertices. Z here is not a distance
             * but 1/w: bigger is nearer, and the background wants to be far. */
            vertex.flags = PVR_CMD_VERTEX;
            vertex.argb = 0xFFFFFFFF;
            vertex.oargb = 0;
            vertex.z = 1.0f;

            vertex.x = left;  vertex.y = bottom; vertex.u = 0.0f; vertex.v = 1.0f;
            pvr_prim(&vertex, sizeof(vertex));

            vertex.x = left;  vertex.y = top;    vertex.u = 0.0f; vertex.v = 0.0f;
            pvr_prim(&vertex, sizeof(vertex));

            vertex.x = right; vertex.y = bottom; vertex.u = 1.0f; vertex.v = 1.0f;
            pvr_prim(&vertex, sizeof(vertex));

            vertex.flags = PVR_CMD_VERTEX_EOL;
            vertex.x = right; vertex.y = top;    vertex.u = 1.0f; vertex.v = 0.0f;
            pvr_prim(&vertex, sizeof(vertex));
        }
    }
}

void scene2d_free(Scene2D* scene) {
    if (!scene->Textures)
        return;

    for (int i = 0; i < scene->Columns * scene->Rows; i++) {
        if (scene->Textures[i])
            pvr_mem_free(scene->Textures[i]);
    }

    free(scene->Textures);
    scene->Textures = NULL;
}
