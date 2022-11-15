#if not defined ENABLE_SLOG
#include <fstream>

#include "logger.h"
#include "DLNAModule.h"
#if _WIN64
#define gettid() GetCurrentThreadId()
#endif //_WIN64

#define LOG_LINE_SIZE 1024
#if LOG_LINE_SIZE <= 64
#error "TOO SMALL BUFFER SIZE MAY CAUSE FATAL ERROR."
#elif LOG_LINE_SIZE <= 256
#warning "MUST ENSURE ALL THE BUFFER NOT TO OVERFLOW."
#endif //LOG_LINE_SIZE 


const char* LOGTAG[] = { "[I]","[W]","[E]" };
void Log(LogTag level, const char* format, ...)
{
	std::ofstream outLogFileStream{ DLNAModule::GetInstance().logFile, std::ios::out | std::ios::app };
	if (!outLogFileStream.is_open())
		return;

	time_t current;
	time(&current);
	struct tm* local = localtime(&current);

	char* logInfo = new char[LOG_LINE_SIZE];
	int offset = snprintf(logInfo, LOG_LINE_SIZE - 1, "%04d/%02d/%02d %02d:%02d:%02d SkyboxSamba: %s[thread %d] ",
		local->tm_year + 1900, local->tm_mon + 1, local->tm_mday,
		local->tm_hour, local->tm_min, local->tm_sec,
		LOGTAG[(int)level], gettid());
	va_list args;
	va_start(args, format);
	vsnprintf(logInfo + offset, LOG_LINE_SIZE - offset - 1, format, args);
	va_end(args);

	outLogFileStream << logInfo << "\n";
	delete[] logInfo;
	outLogFileStream.close();
}
#endif