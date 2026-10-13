#ifdef N3DS

#include <Engine/Platforms/3DS/System.h>

#include <3ds.h>
#include <malloc.h>
#include <string.h>

// libctru divides the application's memory between the ordinary heap and the
// linear heap, the part the GPU and the DSP can read, and unless told otherwise
// gives the linear heap 32 MB. SDL draws in software on the 3DS, so all the
// linear heap holds is the two screens' framebuffers and the audio buffers; the
// rest of it is better spent on the game.
extern "C" {
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;

// Worked out by libctru at startup from what is left.
extern u32 __ctru_heap_size;
}

static bool RomFSMounted = false;

void N3DS_Init() {
	// The original 3DS has neither the faster clock nor the extra cache, and
	// ignores this.
	osSetSpeedupEnable(true);

	// Fails, harmlessly, when the .3dsx was built without one.
	RomFSMounted = R_SUCCEEDED(romfsInit());
}

void N3DS_Quit() {
	if (RomFSMounted) {
		romfsExit();
		RomFSMounted = false;
	}
}

bool N3DS_HasRomFS() {
	return RomFSMounted;
}

void N3DS_GetMemory(size_t* total, size_t* used) {
	*total = __ctru_heap_size;
	*used = mallinfo().uordblks;
}

void N3DS_DebugOutput(const char* text) {
	svcOutputDebugString(text, strlen(text));
}

void N3DS_ShowError(const char* text) {
	errorConf error;
	errorInit(&error, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
	errorText(&error, text);
	errorDisp(&error);
}

#endif /* N3DS */
