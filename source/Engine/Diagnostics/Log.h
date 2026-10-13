#ifndef ENGINE_DIAGNOSTICS_LOG_H
#define ENGINE_DIAGNOSTICS_LOG_H

#include <Engine/Includes/Standard.h>

class Log {
private:
	static FILE* File;
	static bool Initialized;
	static char* Buffer;
	static size_t BufferSize;

	static bool ResizeBuffer(int written_chars);
	static void Remember(int sev, const char* text);

public:
	enum LogLevels {
		LOG_API = -2,
		LOG_VERBOSE = -1,
		LOG_INFO = 0,
		LOG_WARN = 1,
		LOG_ERROR = 2,
		LOG_IMPORTANT = 3,
		LOG_FATAL = 4
	};
	static int LogLevel;
	static bool WriteToFile;

	// How many messages the in-memory history keeps for the editor's console.
	static size_t MaxHistory;

	static void Init();
	static void OpenFile(const char* filename);
	static void Close();
	static void SetLogLevel(int sev);
	static void Print(int sev, const char* format, ...);
	static void PrintSimple(const char* format, ...);
	static size_t GetHistoryCount();
	static const char* GetHistoryText(size_t index);
	static int GetHistorySeverity(size_t index);
	static void ClearHistory();
};

#endif /* ENGINE_DIAGNOSTICS_LOG_H */
