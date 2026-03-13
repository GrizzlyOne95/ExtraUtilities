#pragma once

#include <Windows.h>

#include <cstdarg>
#include <cstdio>

namespace ExtraUtilities::Logging
{
	inline void LogMessage(const char* format, ...)
	{
		char buffer[1024]{};
		va_list args;
		va_start(args, format);
		vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
		va_end(args);

		OutputDebugStringA(buffer);
		OutputDebugStringA("\n");

		FILE* log = nullptr;
		if (fopen_s(&log, "exu.log", "a") == 0 && log)
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
