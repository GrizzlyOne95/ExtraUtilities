#pragma once

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace ExtraUtilities
{
	namespace Logging
	{
		inline std::string GetLogFilePath(const char* path)
		{
			const char* safeName = (path != nullptr && path[0] != '\0') ? path : "exu.log";
			if (const char* slash = std::strrchr(safeName, '\\'))
			{
				safeName = slash + 1;
			}
			if (const char* slash = std::strrchr(safeName, '/'))
			{
				safeName = slash + 1;
			}

			char modulePath[MAX_PATH]{};
			const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
			{
				return safeName;
			}

			char* lastSlash = std::strrchr(modulePath, '\\');
			if (lastSlash == nullptr)
			{
				return safeName;
			}

			*(lastSlash + 1) = '\0';
			const std::string gameRoot(modulePath);
			const std::string logDirectory = gameRoot + "logs";
			if (CreateDirectoryA(logDirectory.c_str(), nullptr) != FALSE ||
				GetLastError() == ERROR_ALREADY_EXISTS)
			{
				return logDirectory + "\\" + safeName;
			}

			return gameRoot + safeName;
		}

		inline void ResetLogFileForCurrentProcess(const char* path)
		{
			if (path == nullptr || path[0] == '\0')
			{
				return;
			}

			const std::string resolvedPath = GetLogFilePath(path);
			static std::mutex mutex;
			static std::unordered_set<std::string> resetPaths;

			std::lock_guard<std::mutex> lock(mutex);
			if (!resetPaths.insert(resolvedPath).second)
			{
				return;
			}

			FILE* log = nullptr;
			if (fopen_s(&log, resolvedPath.c_str(), "w") == 0 && log != nullptr)
			{
				std::fclose(log);
			}
		}

		inline FILE* OpenSessionLogFile(const char* path)
		{
			ResetLogFileForCurrentProcess(path);
			const std::string resolvedPath = GetLogFilePath(path);

			FILE* log = nullptr;
			if (fopen_s(&log, resolvedPath.c_str(), "a") != 0)
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
