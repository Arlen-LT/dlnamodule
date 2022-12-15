#pragma once

#include <mutex>
#include <iostream>
#include <ostream>
#include <fstream>
#include <filesystem>

#if __cpp_lib_format
#include <format>
#endif

#if __cpp_lib_source_location
#include <source_location>
#endif

#if _WIN64
#define gettid() std::this_thread::get_id()
#elif __ANDROID__
#include <android/log.h>
#include <unistd.h>
#endif

#if not ENABLE_SLOG
namespace logger {

    const std::filesystem::path logFile;
    enum class LogLevel : const char
    {
        Trace = 'T',
        Debug = 'D',
        Info = 'I',
        Warning = 'W',
        Error = 'E',
        Fatal = 'F'
    };

    extern "C" bool SetLogFile(const char* path);
    extern "C" void ExternalLog(LogLevel level, const char* format);

    template <typename... Args>
    struct Log
    {
        Log(LogLevel level, const char* format, Args&&... args
#if __cpp_lib_source_location
            , const std::source_location& location = std::source_location::current()
#endif
        )
        {
            std::ofstream logStream{ logFile, std::ios::out | std::ios::app };
            std::ostream* os = logStream.is_open() ? &logStream : &std::cout;
            time_t current = std::time(nullptr);
            *os << std::left
                << std::put_time(std::localtime(&current), "%F %T ")
                << "[" << static_cast<char>(level) << "]"
                << "[thread " << gettid() << "] "
#if __cpp_lib_source_location
                << location.file_name()
                << "(" << location.line() << "," << location.column() << ","
                << std::quoted(location.function_name())
                << "): ";
#endif
#if __cpp_lib_format and ENABLE_STD_FORMAT
            * os << std::vformat(std::string_view{ format }, std::make_format_args(args...)) << "\n";
#else
            std::string_view sv{ format };
            auto it = sv.begin();
            auto TryInsert = [&sv, &it, &os]<typename T>(size_t n, T obj)
            {
                for (; it != sv.end(); ++it)
                {
                    if (*it == '%')
                    {
                        *os << obj;
                        ++it;
                        break;
                    }
                    else
                    {
                        *os << *it;
                    }
                }
            };

            [&] <std::size_t... N>(std::index_sequence<N...>)
            {
                ((TryInsert(N, std::get<N>(std::forward_as_tuple(args...)))), ...);
            }(std::index_sequence_for<Args...>{});

            *os << "\n";
        }
    };
    template <typename... Args>
    Log(LogLevel, const char*, Args&&...) -> Log<Args...>;
#endif
}
using namespace logger;

#else // ENABLE_SLOG
enum LogLevel
{
    Trace = 16,
    Debug = 8,
    Info = 4,
    Warning = 2,
    Error = 1,
    Fatal = 0
};
#include "../contrib/slog/slog.h"
#define Log(x, y, ...) do {                                  \
    slog_tag("Log", 3 - x, y"\n", ##__VA_ARGS__);            \
} while (0)
#endif // ENABLE_SLOG

#define CHECK_VARIABLE(x, y) do {                             \
   Log(LogLevel::Debug, #x": " y, x);                         \
} while (0);

