/* SEGA Saturn runtime for Hatch Game Engine exports
 * 
 * This provides a minimal runtime that displays the exported scene art
 * and allows navigation with the controller. The game logic goes here.
 *
 * Build with the SEGA Saturn SDK: https://github.com/SaturnSDK
 */

#ifndef __SATURN_H__
#define __SATURN_H__

#include <stddef.h>
#include <stdint.h>

typedef unsigned char uint8;
typedef unsigned short int uint16;
typedef unsigned long int uint32;

typedef signed char int8;
typedef signed short int int16;
typedef signed long int int32;

typedef volatile unsigned char vuint8;
typedef volatile unsigned short int vuint16;
typedef volatile unsigned long int vuint32;

/* Saturn screen dimensions */
#define SATURN_SCREEN_WIDTH     352
#define SATURN_SCREEN_HEIGHT    224
#define SATURN_MAX_IMAGE_BYTES  (512 * 1024)  /* 512 KB max for image data */
#define SATURN_PALETTE_USABLE   255           /* Max colors usable (one reserved for transparency) */

/* Create a 16-bit RGB color (5:5:5 with MSB unused) */
#define COLOR_RGB(r,g,b)    (((r)&0x1F)|((g)&0x1F)<<5|((b)&0x1F)<<10)

/* Color quantization macro for 5 bits per channel */
#define SATURN_COLOR_WORD(r,g,b)  ((((r)>>3)&0x1F)|(((g)>>3)&0x1F)<<5|(((b)>>3)&0x1F)<<10)

#endif /* __SATURN_H__ */
