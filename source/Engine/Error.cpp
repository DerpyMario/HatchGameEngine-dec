#include <Engine/Application.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Error.h>
#include <Engine/Includes/Standard.h>
#include <Engine/Includes/StandardSDL2.h>

#ifdef N3DS
#include <Engine/Platforms/3DS/System.h>
#endif

void Error::ShowFatal(const char* errorString, bool showMessageBox) {
	Log::Print(Log::LOG_FATAL, "%s", errorString);

	if (showMessageBox) {
#ifdef N3DS
		// SDL has no message box on the 3DS, and the screens are SDL's until
		// the game quits, so the system's error screen says it instead.
		N3DS_ShowError(errorString);
#else
		// This doesn't check the return code because the error is already logged.
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Error", errorString, nullptr);
#endif
	}

	Application::Cleanup();
	exit(-1);
}

void Error::Fatal(const char* errorMessage, ...) {
	va_list args;
	char errorString[2048];
	va_start(args, errorMessage);
	vsnprintf(errorString, sizeof(errorString), errorMessage, args);
	va_end(args);

	ShowFatal(errorString, true);
}
void Error::FatalNoMessageBox(const char* errorMessage, ...) {
	va_list args;
	char errorString[2048];
	va_start(args, errorMessage);
	vsnprintf(errorString, sizeof(errorString), errorMessage, args);
	va_end(args);

	ShowFatal(errorString, false);
}
