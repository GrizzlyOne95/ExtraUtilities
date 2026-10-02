/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

// The failure count and reporting every tests/host program shares. Each test
// is its own executable, so the count is per program.

#include <cstdlib>
#include <iostream>
#include <string>

namespace HostTest
{
	inline int& FailureCount() noexcept
	{
		static int count = 0;
		return count;
	}

	// For checks that print their own, more specific, message.
	inline void CountFailure() noexcept
	{
		++FailureCount();
	}

	inline void Expect(bool condition, const std::string& what)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << what << '\n';
			CountFailure();
		}
	}

	// Prints the summary and returns main's exit status.
	inline int Finish(const char* suite)
	{
		if (FailureCount() != 0)
		{
			std::cerr << FailureCount() << ' ' << suite << " check(s) failed\n";
			return EXIT_FAILURE;
		}

		std::cout << "All " << suite << " checks passed.\n";
		return EXIT_SUCCESS;
	}
}
