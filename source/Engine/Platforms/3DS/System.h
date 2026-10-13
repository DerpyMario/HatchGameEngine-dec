#ifndef ENGINE_PLATFORMS_3DS_SYSTEM_H
#define ENGINE_PLATFORMS_3DS_SYSTEM_H

#ifdef N3DS

#include <stddef.h>

// What the engine needs from libctru, behind plain functions so that <3ds.h>
// -- which names types like Thread and Handle -- stays out of the engine's own
// files.

// Turns on the New 3DS's faster clock and mounts the RomFS, if the .3dsx has
// one.
void N3DS_Init();
void N3DS_Quit();

// Whether the .3dsx carries a RomFS, which is where an exported game's data
// lives.
bool N3DS_HasRomFS();

// The heap the game allocates from, and how much of it is in use, in bytes.
// It is what is left of the memory the system gave the game once the code and
// the linear heap have had theirs, which on an original 3DS is not much.
void N3DS_GetMemory(size_t* total, size_t* used);

// Sends a line to the debugger: Luma3DS's, or an emulator's log.
void N3DS_DebugOutput(const char* text);

// Shows the system's own error screen with the given message. Returns once the
// player has dismissed it.
void N3DS_ShowError(const char* text);

#endif /* N3DS */

#endif /* ENGINE_PLATFORMS_3DS_SYSTEM_H */
