#pragma once

#if ENABLE_SLOG
#include "../contrib/slog/slog.h"
#define Log(x, y, ...) do {                                 \
    slog_tag("DLNAModule", 3 - x, y"\n", ##__VA_ARGS__);     \
} while (0)
#else
enum LogTag {
	LEVEL_INFO,
	LEVEL_WARNING,
	LEVEL_ERROR
};
void Log(LogTag level, const char* format, ...);
#endif