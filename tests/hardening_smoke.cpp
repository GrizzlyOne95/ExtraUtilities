#include "BasicPatch.h"
#include "Hook.h"
#include "Scanner.h"
#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <vector>

using namespace ExtraUtilities;

namespace
{
	class BytePatch final : public BasicPatch
	{
	private:
		uint8_t value;

		void DoPatch() override
		{
			if (!CanPatch() || !ValidatePreimage())
			{
				return;
			}

			auto* target = reinterpret_cast<uint8_t*>(m_address);
			DWORD oldProtect{};
			if (!VirtualProtect(target, m_length, PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				return;
			}

			*target = value;
			FlushPatchedRange();
			VirtualProtect(target, m_length, oldProtect, &dummyProtect);
			m_status = Status::ACTIVE;
		}

	public:
		BytePatch(uint8_t* address, uint8_t replacement, Status status, std::vector<uint8_t> expected = {})
			: BasicPatch(reinterpret_cast<uintptr_t>(address), 1, status, std::move(expected)),
			  value(replacement)
		{
			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}
	};

	bool Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			return false;
		}
		return true;
	}
}

int main()
{
	static_assert(!std::is_move_constructible_v<Hook>);
	static_assert(!std::is_move_constructible_v<Scanner<int>>);

	bool ok = true;

	auto* patchPage = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!patchPage)
	{
		std::cerr << "VirtualAlloc failed\n";
		return 1;
	}
	patchPage[0] = 0x11;

	BasicPatch::UnloadAllPatches();
	{
		BytePatch patch(patchPage, 0x22, BasicPatch::Status::ACTIVE, { 0x11 });
		ok &= Check(patchPage[0] == 0x11, "patch activated before deferred activation");

		BasicPatch::EnableDeferredPatchActivation();
		ok &= Check(patchPage[0] == 0x22 && patch.IsActive(), "deferred patch did not activate");

		{
			ScopedPatchDisable disabled(patch);
			ok &= Check(patchPage[0] == 0x11 && !patch.IsActive(), "scoped disable did not restore preimage");
		}
		ok &= Check(patchPage[0] == 0x22 && patch.IsActive(), "scoped disable did not reactivate patch");

		BasicPatch::UnloadAllPatches();
		ok &= Check(patchPage[0] == 0x11 && !patch.IsActive(), "UnloadAllPatches did not restore bytes");

		patch.SetStatus(BasicPatch::Status::ACTIVE);
		ok &= Check(patchPage[0] == 0x11, "SetStatus reactivated while global activation was disabled");

		BasicPatch::EnableDeferredPatchActivation();
		ok &= Check(patchPage[0] == 0x22, "requested active state was not restored on enable");
		patch.SetStatus(false);
		ok &= Check(patchPage[0] == 0x11, "boolean SetStatus(false) did not unload patch");
	}
	BasicPatch::UnloadAllPatches();

	patchPage[0] = 0x33;
	{
		BytePatch rejected(patchPage, 0x44, BasicPatch::Status::ACTIVE, { 0x99 });
		BasicPatch::EnableDeferredPatchActivation();
		ok &= Check(patchPage[0] == 0x33 && !rejected.IsActive(), "expected-byte mismatch did not fail closed");
	}
	BasicPatch::UnloadAllPatches();

	auto* root = static_cast<uintptr_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	auto* middle = static_cast<uintptr_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	auto* finalValue = static_cast<int*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!root || !middle || !finalValue)
	{
		std::cerr << "pointer-chain VirtualAlloc failed\n";
		return 1;
	}

	*root = reinterpret_cast<uintptr_t>(middle);
	*middle = reinterpret_cast<uintptr_t>(finalValue);
	*finalValue = 7;

	MEMORY_BASIC_INFORMATION before{};
	MEMORY_BASIC_INFORMATION after{};
	VirtualQuery(finalValue, &before, sizeof(before));
	{
		Scanner<int> scanner(reinterpret_cast<int*>(root), { 0, 0 }, BasicScanner::Restore::ENABLED);
		ok &= Check(scanner.Get() == finalValue, "scanner resolved the wrong final pointee");
		ok &= Check(scanner.Read() == 7, "scanner read failed");
		scanner.Write(9);
		ok &= Check(*finalValue == 9, "scanner write failed");
	}
	VirtualQuery(finalValue, &after, sizeof(after));
	ok &= Check(*finalValue == 7, "scanner destructor did not restore original value");
	ok &= Check(before.Protect == after.Protect, "scanner restored memory protection to the wrong page/protection");

	const std::array<uint8_t, 8> bytes{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
	const std::array<uint8_t, 3> pattern{ 0x30, 0x00, 0x50 };
	const std::array<uint8_t, 3> mask{ 1, 0, 1 };
	const uintptr_t found = SignatureResolver::FindMaskedPattern(
		bytes.data(), bytes.size(), reinterpret_cast<uintptr_t>(bytes.data()),
		pattern.data(), mask.data(), pattern.size());
	ok &= Check(found == reinterpret_cast<uintptr_t>(bytes.data() + 2), "masked signature resolver returned wrong match");

	VirtualFree(patchPage, 0, MEM_RELEASE);
	VirtualFree(root, 0, MEM_RELEASE);
	VirtualFree(middle, 0, MEM_RELEASE);
	VirtualFree(finalValue, 0, MEM_RELEASE);

	if (!ok)
	{
		return 1;
	}

	std::cout << "EXU hardening smoke tests passed\n";
	return 0;
}
