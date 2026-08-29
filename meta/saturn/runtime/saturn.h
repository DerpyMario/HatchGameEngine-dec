/* Just enough SEGA Saturn to put a picture on the screen.
 *
 * This is bare metal: no SGL, no SBL, no SDK. Everything below is a register
 * the hardware documents, written to directly. The SaturnSDK toolchain builds
 * it, but so does any sh-elf-gcc, which is the point -- an export should not
 * need a library nobody can redistribute.
 *
 * The Saturn has two video chips. VDP2 draws backgrounds: tilemaps, or a flat
 * bitmap, which is what a converted scene layer becomes. VDP1 draws sprites and
 * polygons into a framebuffer VDP2 then composites, and it is what the 3D
 * export uses.
 */

#ifndef SATURN_H
#define SATURN_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed char        s8;
typedef signed short       s16;
typedef signed int         s32;
typedef unsigned long long u64;
typedef signed long long   s64;

/* The SH-2 sees the hardware uncached through the 0x2... mirror. Registers have
 * to be read and written there or a write can sit in cache and never land. */
#define VDP1_VRAM      ((volatile u8  *)0x25C00000)
#define VDP1_FB        ((volatile u16 *)0x25C80000)
#define VDP1_REG       ((volatile u16 *)0x25D00000)
#define VDP2_VRAM      ((volatile u8  *)0x25E00000)
#define VDP2_CRAM      ((volatile u16 *)0x25F00000)
#define VDP2_REG       ((volatile u16 *)0x25F80000)
#define SMPC_REG       ((volatile u8  *)0x20100000)

/* VDP1 registers, by word index. */
#define VDP1_TVMR      0x00
#define VDP1_FBCR      0x01
#define VDP1_PTMR      0x02
#define VDP1_EWDR      0x03
#define VDP1_EWLR      0x04
#define VDP1_EWRR      0x05
#define VDP1_ENDR      0x06

/* VDP2 registers, by word index. */
#define VDP2_TVMD      0x000
#define VDP2_EXTEN     0x001
#define VDP2_VRSIZE    0x003
#define VDP2_RAMCTL    0x007
#define VDP2_CYCA0L    0x008
#define VDP2_CYCA0U    0x009
#define VDP2_CYCA1L    0x00A
#define VDP2_CYCA1U    0x00B
#define VDP2_CYCB0L    0x00C
#define VDP2_CYCB0U    0x00D
#define VDP2_CYCB1L    0x00E
#define VDP2_CYCB1U    0x00F
#define VDP2_BGON      0x010
#define VDP2_CHCTLA    0x014
#define VDP2_CHCTLB    0x015
#define VDP2_BMPNA     0x016
#define VDP2_MPOFN     0x01E
#define VDP2_SCXIN0    0x038
#define VDP2_SCXDN0    0x039
#define VDP2_SCYIN0    0x03A
#define VDP2_SCYDN0    0x03B
#define VDP2_ZMXIN0    0x03C
#define VDP2_ZMXDN0    0x03D
#define VDP2_ZMYIN0    0x03E
#define VDP2_ZMYDN0    0x03F
#define VDP2_BKTAU     0x056
#define VDP2_BKTAL     0x057
#define VDP2_SPCTL     0x070
#define VDP2_CRAOFA    0x072
#define VDP2_CRAOFB    0x073
#define VDP2_PRISA     0x078
#define VDP2_PRINA     0x07C
#define VDP2_PRINB     0x07D

/* NTSC 352x224. The Saturn calls this "hi-res" horizontally and 224 lines is
 * what NTSC gives you without overscan tricks. */
#define SCREEN_W       352
#define SCREEN_H       224

/* The VDP2 bitmap NBG0 draws from. Its size is one of four the chip offers, and
 * 1024x256 is the one that holds a 352-wide screen with room to scroll. */
#define BITMAP_W       1024
#define BITMAP_H       256

/* Where in VDP2 VRAM the bitmap lives. MPOFN counts these in 0x20000 steps, so
 * bank 0 is the start of VRAM. */
#define BITMAP_ADDR    0x00000

/* One word just past the bitmap holds the colour the back screen shows. The
 * bitmap's index 0 is transparent, so this is what is seen through it. */
#define BACKSCREEN_ADDR 0x40000

/* A Saturn colour: five bits a channel, red in the low bits. Bit 15 marks a
 * colour as opaque where the hardware cares; CRAM entries ignore it. */
#define RGB555(r, g, b) ((u16)(((r) & 0x1F) | (((g) & 0x1F) << 5) | (((b) & 0x1F) << 10)))

void  vdp_init_bitmap(void);
void  vdp_load_palette(const u16* colours, int count);
void  vdp_blit(const u8* image, int imageW, int imageH, int cameraX, int cameraY);
void  vdp_wait_vblank(void);
void  vdp_scroll(int x, int y);

void  vdp1_init(void);
void  vdp1_begin(void);
void  vdp1_quad(s16 ax, s16 ay, s16 bx, s16 by, s16 cx, s16 cy, s16 dx, s16 dy, u16 colour);
void  vdp1_end(void);

/* The digital pad, as SMPC reports it. */
#define PAD_RIGHT  0x8000
#define PAD_LEFT   0x4000
#define PAD_DOWN   0x2000
#define PAD_UP     0x1000
#define PAD_START  0x0800
#define PAD_A      0x0400
#define PAD_C      0x0200
#define PAD_B      0x0100
#define PAD_R      0x0080
#define PAD_X      0x0040
#define PAD_Y      0x0020
#define PAD_Z      0x0010
#define PAD_L      0x0008

/* ---------------------------------------------------------- 3D scenes --- */

/* 16.16 fixed point. The SH-2 has no floating point unit, so a 3D scene is
 * drawn entirely in integers. */
typedef s32 fixed;
#define FIX(n)       ((fixed)((n) * 65536))
#define FMUL(a, b)   ((fixed)(((s64)(a) * (b)) >> 16))

#define MESH3D_MAX_VERTICES 4096
#define MESH3D_MAX_FACES    2000

/* How far the eye is from the screen, and the closest anything may come to it
 * before the perspective divide stops meaning anything. */
#define MESH3D_FOCAL        FIX(220)
#define MESH3D_NEAR         FIX(16)

typedef struct {
    const u8* Vertices;
    const u8* Faces;
    int       VertexCount;
    int       FaceCount;
} Mesh3D;

fixed fsin(int angle);
fixed fcos(int angle);

int   mesh3d_open(Mesh3D* mesh, const u8* blob);
void  mesh3d_draw(const Mesh3D* mesh, int yaw, int pitch, s32 distance);
int   mesh3d_visible_faces(void);

void  pad_init(void);
u16   pad_read(void);

void* memset(void* s, int c, unsigned long n);
void* memcpy(void* d, const void* s, unsigned long n);

#endif /* SATURN_H */
