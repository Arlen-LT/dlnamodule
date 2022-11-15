#pragma once

enum LogTag {
	LEVEL_INFO,
	LEVEL_WARNING,
	LEVEL_ERROR
};

#if ENABLE_SLOG
#include "../contrib/slog/slog.h"
#define Log(x, y, ...) do {                                 \
    slog_tag("DLNAModule", 3 - x, y"\n", ##__VA_ARGS__);     \
} while (0)
#else
void Log(LogTag level, const char* format, ...);
#endif