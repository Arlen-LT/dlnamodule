#include "logger.h"

#if not ENABLE_SLOG
namespace logger {

    bool SetLogFile(const char* path)
    {
        return std::filesystem::exists(const_cast<std::filesystem::path&>(logFile) = std::filesystem::u8path(path)); //TODO: change all string to u8string.
    }

    extern "C" void ExternalLog(LogLevel level, const char* format)
    {
        Log(level, format);
    }

}// namespace logger
#endif // ENABLE_SLOG
