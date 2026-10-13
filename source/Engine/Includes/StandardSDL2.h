#ifndef STANDARDSDL2_H
#define STANDARDSDL2_H

#if WIN32
// SDL2 includes
#include <SDL2/SDL.h>
#elif MACOSX
// SDL2 includes
#include <SDL2/SDL.h>
#elif SWITCH
// SDL2 includes
#include <SDL2/SDL.h>
// Important includes
#include <switch.h>
#elif IOS
// SDL2 includes
#include "SDL.h"
#elif ANDROID
// SDL2 includes
#include <SDL.h>
#include <SDL_opengles2.h>
#elif XBOX
// SDL2 includes
#include <SDL.h>
#elif EMSCRIPTEN
// SDL2 includes
#include <SDL.h>
#elif N3DS
// SDL2 includes. libctru's own <3ds.h> is left to the files that need it: it
// names types like Thread and Handle that would collide with the engine's.
#include <SDL.h>
#elif LINUX
// SDL2 includes
#include <SDL.h>
#endif

// devkitARM's C library already has a uint, and it is unsigned int, where
// uint32_t there is unsigned long, so the two cannot both be declared.
#if !N3DS
typedef uint32_t uint;
#endif

#endif // STANDARDSDL2_H
