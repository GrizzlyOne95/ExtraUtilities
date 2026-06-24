#pragma once

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>

namespace ExtraUtilities
{
	namespace Logging
	{
		inline void ResetLogFileForCurrentProcess(const char* path)
		{
			if (path == nullptr || path[0] == '\0')
			{
				return;
			}

			static std::mutex mutex;
			static std::unordered_set<std::string> resetPaths;

			std::lock_guard<std::mutex> lock(mutex);
			if (!resetPaths.insert(path).second)
			{
				return;
			}

			FILE* log = nullptr;
			if (fopen_s(&log, path, "w") == 0 && log != nullptr)
			{
				std::fclose(log);
			}
		}

		inline FILE* OpenSessionLogFile(const char* path)
		{
			ResetLogFileForCurrentProcess(path);

			FILE* log = nullptr;
			if (fopen_s(&log, path, "a") != 0)
			{
				return nullptr;
			}

			return log;
		}

		inline void LogMessage(const char* format, ...)
		{
			char buffer[1024]{};
			va_list args;
			va_start(args, format);
			vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
			va_end(args);

			OutputDebugStringA(buffer);
			OutputDebugStringA("\n");

			FILE* log = OpenSessionLogFile("exu.log");
			if (log != nullptr)
			{
				SYSTEMTIME local_time{};
				GetLocalTime(&local_time);
				std::fprintf(
					log,
					"[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
					local_time.wYear,
					local_time.wMonth,
					local_time.wDay,
					local_time.wHour,
					local_time.wMinute,
					local_time.wSecond,
					buffer
				);
				std::fclose(log);
			}
		}
	}
}
