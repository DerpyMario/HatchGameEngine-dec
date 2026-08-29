/* VDP2 for the background, VDP1 for polygons. */

#include "saturn.h"

#define VDP2_TVSTAT 0x002

/* NBG0 as a flat 8-bit bitmap, its colours from the first 256 CRAM entries.
 *
 * The Saturn will not show a background whose priority is zero, which is the
 * value it powers on with -- the single most common reason a first Saturn
 * program draws nothing at all. */
void vdp_init_bitmap(void) {
    int i;

    /* Display off while the registers are in an in-between state. */
    VDP2_REG[VDP2_TVMD] = 0x0000;

    /* Colour RAM as 2048 words of five bits a channel, so CRAM is a flat
     * array and index n of the bitmap is simply CRAM[n]. */
    VDP2_REG[VDP2_RAMCTL] = 0x1000;

    /* Bitmap reads its own VRAM; nothing else needs an access slot. */
    VDP2_REG[VDP2_CYCA0L] = 0xFFFF;
    VDP2_REG[VDP2_CYCA0U] = 0xFFFF;
    VDP2_REG[VDP2_CYCA1L] = 0xFFFF;
    VDP2_REG[VDP2_CYCA1U] = 0xFFFF;
    VDP2_REG[VDP2_CYCB0L] = 0xFFFF;
    VDP2_REG[VDP2_CYCB0U] = 0xFFFF;
    VDP2_REG[VDP2_CYCB1L] = 0xFFFF;
    VDP2_REG[VDP2_CYCB1U] = 0xFFFF;

    /* NBG0 on, nothing else. */
    VDP2_REG[VDP2_BGON] = 0x0001;

    /* N0BMEN (bit 1) turns the bitmap on, N0BMSZ (bits 2-3) = 2 makes it
     * 1024x256, N0CHCN (bits 4-6) = 1 makes it 256 colours. */
    VDP2_REG[VDP2_CHCTLA] = 0x0002 | (2 << 2) | (1 << 4);

    /* Palette bank 0 for the bitmap, so colour n is CRAM[n]. */
    VDP2_REG[VDP2_BMPNA] = 0x0000;

    /* Bank 0 of VRAM holds it. MPOFN counts in 0x20000 steps. */
    VDP2_REG[VDP2_MPOFN] = BITMAP_ADDR / 0x20000;

    VDP2_REG[VDP2_SCXIN0] = 0;
    VDP2_REG[VDP2_SCXDN0] = 0;
    VDP2_REG[VDP2_SCYIN0] = 0;
    VDP2_REG[VDP2_SCYDN0] = 0;

    /* No zoom: 1.0 in 16.16, split across the integer and fraction halves. */
    VDP2_REG[VDP2_ZMXIN0] = 1;
    VDP2_REG[VDP2_ZMXDN0] = 0;
    VDP2_REG[VDP2_ZMYIN0] = 1;
    VDP2_REG[VDP2_ZMYDN0] = 0;

    /* Priority 1. Zero would hide it. */
    VDP2_REG[VDP2_PRINA] = 0x0001;

    /* Sprites above the background, so VDP1's polygons land on top. */
    VDP2_REG[VDP2_PRISA] = 0x0002;
    VDP2_REG[VDP2_SPCTL] = 0x0020;

    /* Index 0 of a VDP2 background is transparent unless told otherwise, and
     * what shows through it is the back screen -- which powers on pointing at
     * whatever VRAM happens to hold. Left alone that is a coloured haze behind
     * every transparent pixel, so it gets a word of its own set to black. */
    *(volatile u16*)&VDP2_VRAM[BACKSCREEN_ADDR] = RGB555(0, 0, 0);
    VDP2_REG[VDP2_BKTAU] = (u16)((BACKSCREEN_ADDR / 2) >> 16) & 0x0003;
    VDP2_REG[VDP2_BKTAL] = (u16)((BACKSCREEN_ADDR / 2) & 0xFFFF);

    for (i = 0; i < BITMAP_W * BITMAP_H; i++)
        VDP2_VRAM[BITMAP_ADDR + i] = 0;

    /* 352x224, NTSC, display on. */
    VDP2_REG[VDP2_TVMD] = 0x8001;
}

void vdp_load_palette(const u16* colours, int count) {
    int i;

    if (count > 256)
        count = 256;

    for (i = 0; i < count; i++)
        VDP2_CRAM[i] = colours[i];
}

void vdp_scroll(int x, int y) {
    VDP2_REG[VDP2_SCXIN0] = (u16)(x & 0x7FF);
    VDP2_REG[VDP2_SCYIN0] = (u16)(y & 0x7FF);
}

/* Copies the window the camera is over into the bitmap.
 *
 * The picture is bigger than the bitmap the VDP2 can hold, so what is on screen
 * is copied rather than scrolled to. That costs a screenful of bytes every time
 * the camera moves, which is why the caller only calls it when it has. */
void vdp_blit(const u8* image, int imageW, int imageH, int cameraX, int cameraY) {
    int y, x;

    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;
    if (cameraX > imageW - SCREEN_W) cameraX = imageW - SCREEN_W;
    if (cameraY > imageH - SCREEN_H) cameraY = imageH - SCREEN_H;
    if (cameraX < 0) cameraX = 0;
    if (cameraY < 0) cameraY = 0;

    for (y = 0; y < SCREEN_H; y++) {
        const u8* src;
        volatile u8* dst = &VDP2_VRAM[BITMAP_ADDR + y * BITMAP_W];

        if (y + cameraY >= imageH) {
            for (x = 0; x < SCREEN_W; x++)
                dst[x] = 0;
            continue;
        }

        src = &image[(y + cameraY) * imageW + cameraX];

        for (x = 0; x < SCREEN_W; x++)
            dst[x] = (x + cameraX < imageW) ? src[x] : 0;
    }
}

void vdp_wait_vblank(void) {
    /* Down first, so a call landing inside a blank waits for the next one
     * rather than returning immediately. */
    while (VDP2_REG[VDP2_TVSTAT] & 0x0008)
        ;
    while (!(VDP2_REG[VDP2_TVSTAT] & 0x0008))
        ;
}

/* ------------------------------------------------------------- VDP1 --- */

/* VDP1 draws from a table of 32-byte commands in its own VRAM, each one linked
 * to the next, ending with a command whose top bit says stop. */
#define VDP1_CMD_TABLE  0x00000
#define VDP1_MAX_CMDS   2048

static int CommandCount;

static void vdp1_write(int index, int field, u16 value) {
    volatile u16* cmd = (volatile u16*)&VDP1_VRAM[VDP1_CMD_TABLE + index * 32];
    cmd[field] = value;
}

void vdp1_init(void) {
    /* Normal TV mode, 16-bit colour framebuffer. */
    VDP1_REG[VDP1_TVMR] = 0x0000;

    /* Erase the framebuffer to transparent, and swap on every vblank. */
    VDP1_REG[VDP1_EWDR] = 0x0000;
    VDP1_REG[VDP1_EWLR] = 0x0000;
    VDP1_REG[VDP1_EWRR] = 0x50FF;

    VDP1_REG[VDP1_FBCR] = 0x0000;
    VDP1_REG[VDP1_PTMR] = 0x0002;
}

/* Starts a frame's list with the two commands every list wants: what to clip
 * against, and where the origin sits. */
void vdp1_begin(void) {
    CommandCount = 0;

    /* System clipping to the screen. */
    vdp1_write(0, 0, 0x0009);
    vdp1_write(0, 1, (u16)((32 * 1) >> 3));
    vdp1_write(0, 10, SCREEN_W - 1);
    vdp1_write(0, 11, SCREEN_H - 1);

    /* Local coordinates at the middle of the screen, so a projected point of
     * zero is the centre and the maths above does not have to know. */
    vdp1_write(1, 0, 0x000A);
    vdp1_write(1, 1, (u16)((32 * 2) >> 3));
    vdp1_write(1, 6, SCREEN_W / 2);
    vdp1_write(1, 7, SCREEN_H / 2);

    CommandCount = 2;
}

/* One flat-shaded quad. A triangle is drawn as a quad with its last two corners
 * in the same place, which is how the Saturn has always done triangles. */
void vdp1_quad(s16 ax, s16 ay, s16 bx, s16 by, s16 cx, s16 cy, s16 dx, s16 dy, u16 colour) {
    int i = CommandCount;

    if (i >= VDP1_MAX_CMDS - 1)
        return;

    vdp1_write(i, 0, 0x0004);                    /* polygon */
    vdp1_write(i, 1, (u16)((32 * (i + 1)) >> 3));
    vdp1_write(i, 2, 0x00C0);                    /* end codes off, draw every pixel */
    vdp1_write(i, 3, (u16)(0x8000 | colour));    /* the top bit is what makes it opaque */
    vdp1_write(i, 6, (u16)ax);
    vdp1_write(i, 7, (u16)ay);
    vdp1_write(i, 8, (u16)bx);
    vdp1_write(i, 9, (u16)by);
    vdp1_write(i, 10, (u16)cx);
    vdp1_write(i, 11, (u16)cy);
    vdp1_write(i, 12, (u16)dx);
    vdp1_write(i, 13, (u16)dy);

    CommandCount = i + 1;
}

void vdp1_end(void) {
    /* The command that stops the list. */
    vdp1_write(CommandCount, 0, 0x8000);
}
