/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

// Exception layering for calls into Ogre and the engine.
//
// SEH (__try/__except) is the barrier for hardware faults: an access violation
// from a stale Ogre or engine pointer. It must not be the barrier for C++
// exceptions. Under /EHsc a C++ throw is an SEH exception with code 0xE06D7363;
// an unconditional EXCEPTION_EXECUTE_HANDLER catches it without destroying the
// thrown object, loses its message, and reports an Ogre::Exception for bad
// input (unknown particle template, missing material, bad bone) as a crash.
//
// The layering every guarded call uses:
//
//   1. The function that holds __try (the "SEH shell") filters with
//      Seh::Filter(GetExceptionCode()). It handles faults and passes C++
//      exceptions and stack overflow on.
//   2. Its caller, a separate function with no __try (C2712/C2713 forbid mixing
//      the two), catches the C++ exception with Seh::CatchCpp, which logs what()
//      and returns the shell's failure result. The exception object is
//      destroyed normally.
//
// So a shell `bool TryFoo(...)` becomes `bool TryFooSeh(...)` plus
//
//   bool TryFoo(...)
//   {
//       return Seh::CatchCpp("TryFoo", [&] { return TryFooSeh(...); }, false);
//   }
//
// A shell that only reads raw memory, where nothing can throw, needs no
// wrapper; it still uses the shared filter.
//
// Lua bindings have their own outer barrier (LuaCppBarrier.h), so nothing can
// cross into the C Lua core as a C++ exception even when a binding allocates.
//
// This header is also compiled by OgreNativeFontBridge.cpp, which is pinned to
// C++14 (see the vcxproj), so it sticks to C++14.

#include "Util/Logging.h"

#include <Windows.h>

#include <cstdio>
#include <exception>
#include <utility>

namespace ExtraUtilities
{
	namespace Seh
	{
		// The exception code MSVC's C++ runtime raises for every `throw`.
		constexpr unsigned long kMsvcCppExceptionCode = 0xE06D7363UL;

		// Whether an EXU __except may handle an exception with this code.
		//
		//  * C++ exceptions go on to the caller's try/catch (see above).
		//  * Stack overflow goes on as well. Handling it would resume on a stack
		//    whose guard page is gone, so the next overflow would kill the
		//    process with no exception at all; better to let the crash reporter
		//    see this one.
		//
		// Everything else (access violations, misaligned or privileged
		// instructions, divide by zero, ...) is handled.
		constexpr int Filter(unsigned long exceptionCode) noexcept
		{
			return (exceptionCode == kMsvcCppExceptionCode || exceptionCode == EXCEPTION_STACK_OVERFLOW)
				? EXCEPTION_CONTINUE_SEARCH
				: EXCEPTION_EXECUTE_HANDLER;
		}

		// Same decision, also recording the code for a caller that reports it.
		inline int Filter(unsigned long exceptionCode, unsigned int& outExceptionCode) noexcept
		{
			outExceptionCode = static_cast<unsigned int>(exceptionCode);
			return Filter(exceptionCode);
		}

		inline int Filter(unsigned long exceptionCode, unsigned long& outExceptionCode) noexcept
		{
			outExceptionCode = exceptionCode;
			return Filter(exceptionCode);
		}

		// Same decision, also recording the code and the faulting instruction
		// address for a caller that reports them. Pass GetExceptionInformation()
		// (valid only inside the __except filter expression).
		inline int Filter(unsigned long exceptionCode, unsigned int& outExceptionCode, void*& outFaultAddress, EXCEPTION_POINTERS* info) noexcept
		{
			outFaultAddress = (info != nullptr && info->ExceptionRecord != nullptr) ? info->ExceptionRecord->ExceptionAddress : nullptr;
			return Filter(exceptionCode, outExceptionCode);
		}

		// Describes the exception currently being handled. Call only from inside
		// a catch handler. Allocation-free, never throws.
		inline void DescribeCurrentException(char* buffer, size_t size) noexcept
		{
			if (buffer == nullptr || size == 0)
			{
				return;
			}

			buffer[0] = '\0';
			try
			{
				throw;
			}
			catch (const std::exception& error)
			{
				const char* what = error.what();
				std::snprintf(buffer, size, "%s", what != nullptr ? what : "(no message)");
			}
			catch (...)
			{
				std::snprintf(buffer, size, "non-standard C++ exception");
			}
		}

		// Logs the exception currently being handled. Call only from inside a
		// catch handler.
		inline void LogCurrentCppException(const char* site) noexcept
		{
			char description[512];
			DescribeCurrentException(description, sizeof(description));
			try
			{
				Logging::LogMessage("[EXU] %s threw a C++ exception: %s", site != nullptr ? site : "?", description);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: C++ exception while logging a C++ exception\n");
			}
		}

		namespace Detail
		{
			// A callable fallback produces the failure result (and may reset
			// output parameters); anything else is the failure value itself.
			template <typename Result, typename Fallback>
			auto MakeFallback(Fallback& fallback, int) -> decltype(static_cast<Result>(fallback()))
			{
				return static_cast<Result>(fallback());
			}

			template <typename Result, typename Fallback>
			Result MakeFallback(Fallback& fallback, long)
			{
				return static_cast<Result>(fallback);
			}
		}

		// Runs `body` (which calls an SEH shell) and turns a C++ exception that
		// escapes it into `fallback`: the failure value, or a callable returning
		// it. The exception is logged and destroyed before this returns.
		template <typename Body, typename Fallback>
		auto CatchCpp(const char* site, Body&& body, Fallback&& fallback) -> decltype(body())
		{
			try
			{
				return body();
			}
			catch (...)
			{
				LogCurrentCppException(site);
			}

			return Detail::MakeFallback<decltype(body())>(fallback, 0);
		}

		// As above for a shell that returns nothing.
		template <typename Body>
		void CatchCpp(const char* site, Body&& body)
		{
			try
			{
				body();
			}
			catch (...)
			{
				LogCurrentCppException(site);
			}
		}

		namespace Detail
		{
			// A generic SEH shell: no C++ objects of its own, so it may hold
			// __try; the callable's own frame may hold any.
			template <typename Body>
			bool RunSeh(Body& body, unsigned long& outExceptionCode)
			{
				__try
				{
					body();
					return true;
				}
				__except (Filter(GetExceptionCode(), outExceptionCode))
				{
					return false;
				}
			}
		}

		// Runs `body` behind both barriers, for code that does not need an SEH
		// shell of its own. Returns true when body completed. After a fault,
		// onFault(exceptionCode) runs (for the caller's own log line) and the
		// result is false; after a C++ exception, which CatchCpp has already
		// logged and destroyed, the result is false and onFault does not run.
		template <typename Body, typename OnFault>
		bool Guard(const char* site, Body&& body, OnFault&& onFault)
		{
			unsigned long exceptionCode = 0;
			const bool completed = CatchCpp(site, [&] { return Detail::RunSeh(body, exceptionCode); }, false);
			if (!completed && exceptionCode != kMsvcCppExceptionCode)
			{
				onFault(exceptionCode);
			}

			return completed;
		}

		template <typename Body>
		bool Guard(const char* site, Body&& body)
		{
			return Guard(site, std::forward<Body>(body), [](unsigned long) {});
		}
	}
}
