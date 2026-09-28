/* Copyright (C) 2023-2026 VTrider
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

#include "OS.h"

#include "EngineAddresses.generated.h"
#include "Logging.h"
#include "NativeSaveFlag.h"
#include "SavePathPolicy.h"
#include "RuntimeGate.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma push_macro("MessageBox")
#undef MessageBox

namespace ExtraUtilities::Lua::OS
{
	namespace
	{
		using NativeSaveGameFn = bool(__cdecl*)(char*, int);
		using NativeSaveShellGameFn = bool(__cdecl*)(int, const char*);
		using MissionSaveFlag = volatile uint8_t*;

		struct BinarySaveState
		{
			// -binarysave command-line option (int) and the byte SaveGame reads
			// to choose the binary writer.
			const volatile int32_t* commandLineSwitch = nullptr;
			volatile uint8_t* saveFlag = nullptr;
		};

		struct ExecutableSection
		{
			const uint8_t* address = nullptr;
			size_t size = 0;
			std::string name;
		};

		constexpr std::array<int, 54> SAVE_GAME_SIGNATURE = {
			0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0xC6, 0x45, 0xFF, 0x01, 0xE8, -1, -1,
			-1, -1, 0x89, 0x85, 0x78, 0xFF, 0xFF, 0xFF, 0x68, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
			0x83, 0xC4, 0x04, 0x0F, 0xB6, 0x05, -1, -1, -1, -1, 0x85, 0xC0, 0x0F, 0x84, -1, -1, -1, -1,
			0x6A, 0x2E
		};

		std::mutex g_nativeSaveLogMutex;

		template <typename... Args>
		void LogNativeSave(std::format_string<Args...> fmt, Args&&... args)
		{
			const auto message = std::format(fmt, std::forward<Args>(args)...);
			std::lock_guard<std::mutex> lock(g_nativeSaveLogMutex);

			OutputDebugStringA(message.c_str());
			OutputDebugStringA("\n");

			ExtraUtilities::Logging::ResetLogFileForCurrentProcess("exu_native_save.log");
			std::ofstream file(
				ExtraUtilities::Logging::GetLogFilePath("exu_native_save.log"),
				std::ios::app);
			if (file.is_open())
			{
				file << message << '\n';
			}
		}

		std::string GetMainModuleDirectory()
		{
			char path[MAX_PATH]{};
			const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
			{
				return {};
			}

			std::string result(path, length);
			const auto slash = result.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				return {};
			}

			result.resize(slash);
			return result;
		}

		// Copies the engine's fixed save-directory buffer. Kept free of C++
		// objects so the SEH guard needs no unwinding.
		bool CopyEngineSaveDirectory(char (&out)[ExtraUtilities::NativeSave::ENGINE_SAVE_DIRECTORY_CAPACITY]) noexcept
		{
			__try
			{
				std::memcpy(out, reinterpret_cast<const void*>(EngineAddresses::SaveGame::saveDirectory), sizeof(out));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// The engine's save directory, "<startup working directory>\save",
		// which the engine fills once at startup and its own slot saves use.
		// Empty when the build gate has not accepted this executable or the
		// buffer cannot be read or holds no terminated string.
		std::string ReadEngineSaveDirectory()
		{
			if (!RuntimeGate::IsSupported())
			{
				return {};
			}

			char buffer[ExtraUtilities::NativeSave::ENGINE_SAVE_DIRECTORY_CAPACITY];
			if (!CopyEngineSaveDirectory(buffer))
			{
				return {};
			}
			return std::string(ExtraUtilities::NativeSave::TerminatedString(buffer, sizeof(buffer)));
		}

		// The file the engine's SaveShellGame writes for this slot. Fails
		// closed when the engine save directory is unreadable or unusable.
		ExtraUtilities::NativeSave::SavePathResult BuildSlotSavePath(int slot)
		{
			return ExtraUtilities::NativeSave::BuildSlotSavePath(ReadEngineSaveDirectory(), slot);
		}

		std::string GetCurrentDirectoryString()
		{
			char path[MAX_PATH]{};
			const DWORD length = GetCurrentDirectoryA(MAX_PATH, path);
			if (length == 0 || length >= MAX_PATH)
			{
				return {};
			}
			return std::string(path, length);
		}

		// A script-supplied path must name a file under the game's Save
		// directory. The game root is the executable's directory; the engine's
		// own saves use the startup working directory, which is normally the
		// same place but may differ with some launchers, so the current working
		// directory and the engine's save directory count too.
		ExtraUtilities::NativeSave::SavePathResult ResolveScriptSavePath(std::string_view requested)
		{
			const auto moduleDirectory = GetMainModuleDirectory();
			std::vector<std::string> roots{ moduleDirectory };
			if (auto workingDirectory = GetCurrentDirectoryString(); !workingDirectory.empty())
			{
				roots.push_back(std::move(workingDirectory));
			}
			auto directories = ExtraUtilities::NativeSave::SaveDirectoriesOf(roots);
			if (auto engineSaveDirectory = ReadEngineSaveDirectory(); !engineSaveDirectory.empty())
			{
				directories.push_back(std::move(engineSaveDirectory));
			}
			return ExtraUtilities::NativeSave::ResolveSavePathInDirectories(requested, moduleDirectory, directories);
		}

		bool EnsureSaveParentDirectory(const std::string& filename)
		{
			const auto parent = std::filesystem::path(filename).parent_path();
			if (parent.empty())
			{
				return true;
			}

			std::error_code error;
			std::filesystem::create_directories(parent, error);
			return !error;
		}

		std::string TrimAsciiWhitespace(std::string_view value)
		{
			size_t start = 0;
			size_t end = value.size();

			while (start < end)
			{
				const char c = value[start];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
				{
					break;
				}
				++start;
			}

			while (end > start)
			{
				const char c = value[end - 1];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
				{
					break;
				}
				--end;
			}

			return std::string(value.substr(start, end - start));
		}

		std::string SanitizeSingleLineText(std::string_view value)
		{
			std::string result;
			result.reserve(value.size());

			for (const char c : value)
			{
				if (c != '\0' && c != '\r' && c != '\n')
				{
					result.push_back(c);
				}
			}

			return result;
		}

		int HexNibbleValue(char c)
		{
			if (c >= '0' && c <= '9')
			{
				return c - '0';
			}
			if (c >= 'A' && c <= 'F')
			{
				return 10 + (c - 'A');
			}
			if (c >= 'a' && c <= 'f')
			{
				return 10 + (c - 'a');
			}
			return -1;
		}

		bool IsHexEncodedText(std::string_view value)
		{
			if (value.empty() || (value.size() % 2) != 0)
			{
				return false;
			}

			for (const char c : value)
			{
				if (HexNibbleValue(c) < 0)
				{
					return false;
				}
			}

			return true;
		}

		std::vector<uint8_t> DecodeHexText(std::string_view value)
		{
			std::vector<uint8_t> decoded;
			decoded.reserve(value.size() / 2);

			for (size_t index = 0; index + 1 < value.size(); index += 2)
			{
				const int upper = HexNibbleValue(value[index]);
				const int lower = HexNibbleValue(value[index + 1]);
				if (upper < 0 || lower < 0)
				{
					return {};
				}

				decoded.push_back(static_cast<uint8_t>((upper << 4) | lower));
			}

			return decoded;
		}

		std::string EncodeHexText(const uint8_t* data, size_t size)
		{
			static constexpr char kHexDigits[] = "0123456789ABCDEF";

			std::string encoded;
			encoded.resize(size * 2);
			for (size_t index = 0; index < size; ++index)
			{
				const uint8_t value = data[index];
				encoded[(index * 2) + 0] = kHexDigits[(value >> 4) & 0x0F];
				encoded[(index * 2) + 1] = kHexDigits[value & 0x0F];
			}
			return encoded;
		}

		bool RewriteTextSaveDescription(const std::string& filename, std::string_view description)
		{
			std::ifstream input(filename, std::ios::binary);
			if (!input.is_open())
			{
				LogNativeSave("[EXU::SaveGame] failed to open saved file for description rewrite path={}", filename);
				return false;
			}

			std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (!input.good() && !input.eof())
			{
				LogNativeSave("[EXU::SaveGame] failed to read saved file for description rewrite path={}", filename);
				return false;
			}

			if (data.empty())
			{
				LogNativeSave("[EXU::SaveGame] saved file is empty, skipping description rewrite path={}", filename);
				return false;
			}

			// The field is a line of its own; a plain find also matched the key
			// inside other lines' text.
			constexpr std::string_view kKey = "saveGameDesc";
			size_t keyPos = data.rfind(kKey, 0) == 0 ? 0 : std::string::npos;
			for (size_t lineStart = data.find('\n'); keyPos == std::string::npos && lineStart != std::string::npos;
				lineStart = data.find('\n', lineStart + 1))
			{
				if (data.compare(lineStart + 1, kKey.size(), kKey) == 0)
				{
					keyPos = lineStart + 1;
				}
			}
			if (keyPos == std::string::npos)
			{
				LogNativeSave("[EXU::SaveGame] saveGameDesc not found, skipping description rewrite path={}", filename);
				return true;
			}

			const size_t equalsPos = data.find('=', keyPos + kKey.size());
			if (equalsPos == std::string::npos)
			{
				LogNativeSave("[EXU::SaveGame] malformed saveGameDesc field, skipping description rewrite path={}", filename);
				return false;
			}

			size_t valueStart = equalsPos + 1;
			while (valueStart < data.size())
			{
				const char c = data[valueStart];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
				{
					break;
				}
				++valueStart;
			}

			size_t valueEnd = valueStart;
			while (valueEnd < data.size() && data[valueEnd] != '\r' && data[valueEnd] != '\n')
			{
				++valueEnd;
			}

			const std::string replacement = SanitizeSingleLineText(description);
			std::string rewrittenValue = replacement;

			const std::string_view currentValue(data.data() + valueStart, valueEnd - valueStart);
			if (IsHexEncodedText(currentValue))
			{
				auto decoded = DecodeHexText(currentValue);
				if (!decoded.empty())
				{
					const auto terminatorIt = std::find(decoded.begin(), decoded.end(), static_cast<uint8_t>(0));
					const size_t writableBytes = (terminatorIt != decoded.end())
						? static_cast<size_t>(std::distance(decoded.begin(), terminatorIt))
						: decoded.size();
					const size_t maxDescriptionBytes = (writableBytes > 0) ? (writableBytes - 1) : 0;
					const size_t copyBytes = (writableBytes == 0)
						? 0
						: (std::min)(replacement.size(), maxDescriptionBytes);

					if (writableBytes > 0)
					{
						std::fill(decoded.begin(), decoded.begin() + writableBytes, static_cast<uint8_t>(0));
						if (copyBytes > 0)
						{
							std::memcpy(decoded.data(), replacement.data(), copyBytes);
						}
					}

					rewrittenValue = EncodeHexText(decoded.data(), decoded.size());
					LogNativeSave(
						"[EXU::SaveGame] rewrote hex saveGameDesc path={} description={} bytes={}",
						filename,
						replacement,
						decoded.size());
				}
			}

			data.replace(valueStart, valueEnd - valueStart, rewrittenValue);

			// Write a sibling file and swap it in: truncating the save in place
			// destroyed the save the user just made if the process died or the
			// disk filled mid-write.
			const std::string temporary = filename + ".exutmp";
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output.is_open())
				{
					LogNativeSave("[EXU::SaveGame] failed to open temporary file for description rewrite path={}", temporary);
					return false;
				}

				output.write(data.data(), static_cast<std::streamsize>(data.size()));
				output.flush();
				if (!output.good())
				{
					output.close();
					DeleteFileA(temporary.c_str());
					LogNativeSave("[EXU::SaveGame] failed to write updated save description path={}", temporary);
					return false;
				}
			}

			if (!MoveFileExA(temporary.c_str(), filename.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				const DWORD error = GetLastError();
				DeleteFileA(temporary.c_str());
				LogNativeSave("[EXU::SaveGame] failed to replace save with rewritten description path={} error={}", filename, error);
				return false;
			}

			LogNativeSave("[EXU::SaveGame] rewrote saveGameDesc path={} description={}", filename, replacement);
			return true;
		}

		std::vector<ExecutableSection> GetExecutableSections()
		{
			std::vector<ExecutableSection> sections;

			HMODULE module = GetModuleHandleA("Battlezone98Redux.exe");
			if (module == nullptr)
			{
				module = GetModuleHandleA(nullptr);
			}

			if (module == nullptr)
			{
				return sections;
			}

			auto* const base = reinterpret_cast<const uint8_t*>(module);
			auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return sections;
			}

			auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE)
			{
				return sections;
			}

			auto* section = IMAGE_FIRST_SECTION(nt);
			for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
			{
				if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				{
					continue;
				}

				const size_t size = std::max<size_t>(section->Misc.VirtualSize, section->SizeOfRawData);
				if (size == 0)
				{
					continue;
				}

				char sectionName[9]{};
				std::memcpy(sectionName, section->Name, sizeof(section->Name));

				sections.push_back({
					base + section->VirtualAddress,
					size,
					sectionName
				});
			}

			return sections;
		}

		const uint8_t* FindPattern(const uint8_t* start, size_t size, const auto& pattern)
		{
			if (start == nullptr || size < pattern.size())
			{
				return nullptr;
			}

			const size_t lastOffset = size - pattern.size();
			for (size_t offset = 0; offset <= lastOffset; ++offset)
			{
				bool matched = true;
				for (size_t index = 0; index < pattern.size(); ++index)
				{
					const int expected = pattern[index];
					if (expected >= 0 && start[offset + index] != static_cast<uint8_t>(expected))
					{
						matched = false;
						break;
					}
				}

				if (matched)
				{
					return start + offset;
				}
			}

			return nullptr;
		}

		NativeSaveGameFn ResolveNativeSaveGame()
		{
			static NativeSaveGameFn cached = nullptr;
			if (cached != nullptr)
			{
				return cached;
			}

			const auto executableSections = GetExecutableSections();
			if (executableSections.empty())
			{
				LogNativeSave("[EXU::SaveGame] failed to enumerate executable sections");
				return nullptr;
			}

			for (const auto& section : executableSections)
			{
				if (const auto* address = FindPattern(section.address, section.size, SAVE_GAME_SIGNATURE))
				{
					LogNativeSave(
						"[EXU::SaveGame] resolved native SaveGame at {} in section {}",
						static_cast<const void*>(address),
						section.name
					);
					cached = reinterpret_cast<NativeSaveGameFn>(const_cast<uint8_t*>(address));
					return cached;
				}
			}

			LogNativeSave("[EXU::SaveGame] failed to resolve native SaveGame signature");
			return nullptr;
		}

		MissionSaveFlag ResolveMissionSaveFlag(NativeSaveGameFn saveGame) noexcept
		{
			static MissionSaveFlag cached = nullptr;
			if (cached != nullptr)
			{
				return cached;
			}

			if (saveGame == nullptr)
			{
				return nullptr;
			}

			// Deriving the address from the already-qualified function avoids a
			// second build-specific address table and fails closed if the target
			// build drifts. The decode itself lives in NativeSaveFlag.h so
			// tests/host can exercise the drift cases without the game.
			const auto* entry = reinterpret_cast<const uint8_t*>(saveGame);
			uint32_t flagAddress = 0;
			__try
			{
				flagAddress = ExtraUtilities::NativeSave::ReadMissionSaveFlagAddress(entry);
				if (flagAddress == 0)
				{
					return nullptr;
				}

				auto* flag = reinterpret_cast<MissionSaveFlag>(static_cast<uintptr_t>(flagAddress));
				if (!ExtraUtilities::NativeSave::IsPlausibleMissionSaveValue(*flag))
				{
					return nullptr;
				}

				cached = flag;
				LogNativeSave("[EXU::SaveGame] resolved missionSave flag at 0x{:08X}", flagAddress);
				return cached;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		NativeSaveShellGameFn ResolveNativeSaveShellGame();

		// Decoded from the stock slot-save wrapper, which is where the engine
		// itself applies -binarysave. Fails closed (both pointers null) when the
		// wrapper or the sequence cannot be found.
		BinarySaveState ResolveBinarySaveState(MissionSaveFlag missionSaveFlag) noexcept
		{
			static BinarySaveState cached;
			if (cached.saveFlag != nullptr)
			{
				return cached;
			}

			const auto saveShellGame = ResolveNativeSaveShellGame();
			if (saveShellGame == nullptr || missionSaveFlag == nullptr)
			{
				return {};
			}

			// Covers the whole wrapper; IsSaveShellGameCandidate scans the same
			// window.
			constexpr size_t kWrapperWindow = 0x140;
			ExtraUtilities::NativeSave::BinarySaveAddresses addresses;
			__try
			{
				if (!ExtraUtilities::NativeSave::FindBinarySaveAddresses(
					reinterpret_cast<const uint8_t*>(saveShellGame),
					kWrapperWindow,
					static_cast<uint32_t>(reinterpret_cast<uintptr_t>(missionSaveFlag)),
					addresses))
				{
					return {};
				}

				auto* saveFlag = reinterpret_cast<volatile uint8_t*>(static_cast<uintptr_t>(addresses.saveFlag));
				auto* commandLineSwitch = reinterpret_cast<const volatile int32_t*>(
					static_cast<uintptr_t>(addresses.commandLineSwitch));
				if (!ExtraUtilities::NativeSave::IsPlausibleMissionSaveValue(*saveFlag))
				{
					return {};
				}
				(void)*commandLineSwitch;

				cached.saveFlag = saveFlag;
				cached.commandLineSwitch = commandLineSwitch;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return {};
			}

			LogNativeSave(
				"[EXU::SaveGame] resolved binarySave flag at 0x{:08X}, -binarysave switch at 0x{:08X}",
				addresses.saveFlag,
				addresses.commandLineSwitch);
			return cached;
		}

		const uint8_t* BacktrackFunctionProlog(const ExecutableSection& section, const uint8_t* address, size_t maxBack = 0x400)
		{
			if (section.address == nullptr || address == nullptr || address < section.address || address >= section.address + section.size)
			{
				return nullptr;
			}

			const uint8_t* cursor = address;
			const uint8_t* lowerBound = (address > section.address + maxBack) ? address - maxBack : section.address;
			while (cursor >= lowerBound + 3)
			{
				if (cursor[0] == 0x55 && cursor[1] == 0x8B && cursor[2] == 0xEC)
				{
					return cursor;
				}
				--cursor;
			}

			return nullptr;
		}

		bool ContainsBytePattern(const uint8_t* start, size_t size, const auto& pattern)
		{
			return FindPattern(start, size, pattern) != nullptr;
		}

		bool IsSaveShellGameCandidate(const uint8_t* functionStart, const uint8_t* saveGameAddress)
		{
			if (functionStart == nullptr || saveGameAddress == nullptr)
			{
				return false;
			}

			constexpr size_t kWindowSize = 0x140;
			constexpr std::array<int, 20> SLOT_RANGE_PATTERN = {
				0x83, 0x7D, 0x08, 0x01, 0x0F, 0x8C, -1, -1, -1, -1,
				0x83, 0x7D, 0x08, 0x0A, 0x0F, 0x8F, -1, -1, -1, -1
			};

			// mov dword ptr [ebp+disp8], saveGameDesc
			constexpr uintptr_t kDescription = EngineAddresses::SaveGame::saveGameDesc;
			constexpr std::array<int, 7> DESCRIPTION_BUFFER_PATTERN = {
				0xC7, 0x45, -1,
				static_cast<int>(kDescription & 0xFF),
				static_cast<int>((kDescription >> 8) & 0xFF),
				static_cast<int>((kDescription >> 16) & 0xFF),
				static_cast<int>((kDescription >> 24) & 0xFF)
			};

			if (!ContainsBytePattern(functionStart, kWindowSize, SLOT_RANGE_PATTERN))
			{
				return false;
			}

			if (!ContainsBytePattern(functionStart, 0x40, DESCRIPTION_BUFFER_PATTERN))
			{
				return false;
			}

			for (size_t offset = 0; offset + 5 <= kWindowSize; ++offset)
			{
				if (functionStart[offset] != 0xE8)
				{
					continue;
				}

				int32_t displacement = 0;
				std::memcpy(&displacement, functionStart + offset + 1, sizeof(displacement));
				const auto* target = functionStart + offset + 5 + displacement;
				if (target == saveGameAddress)
				{
					return true;
				}
			}

			return false;
		}

		NativeSaveShellGameFn ResolveNativeSaveShellGame()
		{
			static NativeSaveShellGameFn cached = nullptr;
			if (cached != nullptr)
			{
				return cached;
			}

			const auto executableSections = GetExecutableSections();
			if (executableSections.empty())
			{
				LogNativeSave("[EXU::SaveGame] failed to enumerate executable sections for SaveShellGame");
				return nullptr;
			}

			const auto saveGame = ResolveNativeSaveGame();
			if (saveGame == nullptr)
			{
				LogNativeSave("[EXU::SaveGame] cannot resolve SaveShellGame without SaveGame");
				return nullptr;
			}

			const auto* saveGameAddress = reinterpret_cast<const uint8_t*>(saveGame);
			for (const auto& section : executableSections)
			{
				if (section.address == nullptr || section.size < 5)
				{
					continue;
				}

				for (size_t offset = 0; offset + 5 <= section.size; ++offset)
				{
					if (section.address[offset] != 0xE8)
					{
						continue;
					}

					int32_t displacement = 0;
					std::memcpy(&displacement, section.address + offset + 1, sizeof(displacement));
					const auto* target = section.address + offset + 5 + displacement;
					if (target != saveGameAddress)
					{
						continue;
					}

					const auto* callSite = section.address + offset;
					const auto* functionStart = BacktrackFunctionProlog(section, callSite);
					if (!IsSaveShellGameCandidate(functionStart, saveGameAddress))
					{
						continue;
					}

					LogNativeSave(
						"[EXU::SaveGame] resolved native SaveShellGame at {} in section {} via call {}",
						static_cast<const void*>(functionStart),
						section.name,
						static_cast<const void*>(callSite)
					);
					cached = reinterpret_cast<NativeSaveShellGameFn>(const_cast<uint8_t*>(functionStart));
					return cached;
				}
			}

			LogNativeSave("[EXU::SaveGame] failed to resolve native SaveShellGame signature");
			return nullptr;
		}

		bool InvokeNativeNormalSaveGame(
			NativeSaveGameFn saveGame,
			MissionSaveFlag missionSaveFlag,
			const BinarySaveState& binarySave,
			char* filename,
			int saveType,
			bool& wroteBinary,
			DWORD& exceptionCode) noexcept
		{
			exceptionCode = 0;
			wroteBinary = false;
			if (saveGame == nullptr || missionSaveFlag == nullptr ||
				binarySave.saveFlag == nullptr || binarySave.commandLineSwitch == nullptr)
			{
				return false;
			}

			// FUN_004fdc80, the stock normal-save wrapper, establishes
			// missionSave=0 before calling SaveGame. Direct Lua callers must do
			// the same because FUN_004fbe90 (mission save) leaves this global
			// set after it returns.
			//
			// The prior value is restored rather than forced to 0, because
			// FUN_004fbe90 sets the flag and only then opens a modal dialog
			// (FUN_0056ad10 -> FUN_005d4690, which runs its own event loop).
			// Anything that saves from inside that window would otherwise clear
			// a flag the engine is still relying on and silently downgrade the
			// user's mission save to a normal one.
			//
			// The wrapper also sets binarySave from the -binarysave option; SaveGame
			// itself never reads the option, so without this every direct save
			// was written as text.
			uint8_t previous = 0;
			uint8_t previousBinary = 0;
			__try
			{
				previous = *missionSaveFlag;
				previousBinary = *binarySave.saveFlag;
				wroteBinary = *binarySave.commandLineSwitch != 0;
				*missionSaveFlag = 0;
				*binarySave.saveFlag = wroteBinary ? 1 : 0;
				const bool saved = saveGame(filename, saveType);
				*binarySave.saveFlag = previousBinary;
				*missionSaveFlag = previous;
				return saved;
			}
			__except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
			{
				__try
				{
					*binarySave.saveFlag = previousBinary;
					*missionSaveFlag = previous;
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
				return false;
			}
		}

		// SaveShellGame (FUN_004FDC80) stores missionSave = 0 and never puts it
		// back, like the direct path did before e11a8ce; restore it the same way.
		bool InvokeNativeSaveShellGame(
			NativeSaveShellGameFn saveShellGame,
			MissionSaveFlag missionSaveFlag,
			int slot,
			const char* description,
			DWORD& exceptionCode) noexcept
		{
			exceptionCode = 0;

			uint8_t previous = 0;
			__try
			{
				if (missionSaveFlag != nullptr)
				{
					previous = *missionSaveFlag;
				}
				const bool saved = saveShellGame(slot, description);
				if (missionSaveFlag != nullptr)
				{
					*missionSaveFlag = previous;
				}
				return saved;
			}
			__except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
			{
				__try
				{
					if (missionSaveFlag != nullptr)
					{
						*missionSaveFlag = previous;
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
				return false;
			}
		}
	}

	int MessageBox(lua_State* L)
	{
		lua_getglobal(L, "tostring");
		lua_pushvalue(L, 1); // string parameter
		lua_call(L, 1, 1);
		const char* message = lua_tostring(L, -1);

		// Own the box by the game's window when it is in front, so the game
		// window is disabled while the box is up (no input re-enters the game
		// mid-callback) and the box stays above an exclusive-fullscreen
		// surface instead of looking like a hang.
		HWND owner = GetForegroundWindow();
		DWORD ownerProcess = 0;
		if (owner == nullptr || GetWindowThreadProcessId(owner, &ownerProcess) == 0 ||
			ownerProcess != GetCurrentProcessId())
		{
			owner = nullptr;
		}
		MessageBoxA(owner, message != nullptr ? message : "", "Extra Utilities",
			MB_OK | MB_APPLMODAL | MB_TOPMOST | MB_SETFOREGROUND);
		return 0;
	}

	int GetScreenResolution(lua_State* L)
	{
		int width = GetSystemMetrics(SM_CXSCREEN);
		int height = GetSystemMetrics(SM_CYSCREEN);

		lua_pushnumber(L, width);
		lua_pushnumber(L, height);

		return 2;
	}

	int SaveGame(lua_State* L)
	{
		// The save routines are found by signature, but the missionSave flag
		// write and the description buffer are build-specific.
		if (!RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "unsupported or modified BZR build");
			return 2;
		}

		std::string filename;
		int slot = 0;
		if (lua_type(L, 1) == LUA_TNUMBER)
		{
			slot = static_cast<int>(luaL_checkinteger(L, 1));
			if (slot < 1 || slot > 10)
			{
				return luaL_error(L, "SaveGame slot must be in range 1-10");
			}

			auto slotPath = BuildSlotSavePath(slot);
			if (!slotPath.ok)
			{
				LogNativeSave("[EXU::SaveGame] cannot build path for slot {}: {}", slot, slotPath.error);
				lua_pushboolean(L, 0);
				lua_pushstring(L, slotPath.error);
				return 2;
			}
			filename = std::move(slotPath.path);
		}
		else
		{
			size_t length = 0;
			const char* rawPath = luaL_checklstring(L, 1, &length);
			auto resolved = ResolveScriptSavePath(std::string_view(rawPath, length));
			if (!resolved.ok)
			{
				LogNativeSave("[EXU::SaveGame] rejected save path {}: {}", std::string_view(rawPath, length), resolved.error);
				lua_pushboolean(L, 0);
				lua_pushstring(L, resolved.error);
				return 2;
			}
			filename = std::move(resolved.path);
		}

		if (filename.empty())
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "save filename resolved to an empty path");
			return 2;
		}

		int saveType = 0;
		std::string description;
		const int top = lua_gettop(L);
		if (top >= 2)
		{
			const int arg2Type = lua_type(L, 2);
			if (arg2Type == LUA_TNUMBER)
			{
				saveType = static_cast<int>(lua_tointeger(L, 2));
			}
			else if (arg2Type == LUA_TSTRING)
			{
				size_t length = 0;
				const char* rawDescription = lua_tolstring(L, 2, &length);
				description.assign(rawDescription, length);
			}
			else if (arg2Type != LUA_TNIL && arg2Type != LUA_TNONE)
			{
				return luaL_error(L, "SaveGame second argument must be a saveType number or description string");
			}
		}

		if (top >= 3)
		{
			const int arg3Type = lua_type(L, 3);
			if (arg3Type == LUA_TSTRING)
			{
				size_t length = 0;
				const char* rawDescription = lua_tolstring(L, 3, &length);
				description.assign(rawDescription, length);
			}
			else if (arg3Type != LUA_TNIL && arg3Type != LUA_TNONE)
			{
				return luaL_error(L, "SaveGame third argument must be a description string");
			}
		}

		description = TrimAsciiWhitespace(description);

		// Native SaveShellGame copies the description unbounded into the
		// 256-byte saveGameDesc global; clamp it to what the engine can hold.
		constexpr size_t kMaxSaveDescriptionLength = 255;
		if (description.size() > kMaxSaveDescriptionLength)
		{
			description.resize(kMaxSaveDescriptionLength);
		}

		if (slot != 0 && !description.empty())
		{
			const auto saveShellGame = ResolveNativeSaveShellGame();
			if (saveShellGame != nullptr)
			{
				if (!EnsureSaveParentDirectory(filename))
				{
					LogNativeSave("[EXU::SaveGame] failed to create parent directory for {}", filename);
					lua_pushboolean(L, 0);
					lua_pushfstring(L, "failed to create parent directory for %s", filename.c_str());
					return 2;
				}

				LogNativeSave("[EXU::SaveGame] calling native SaveShellGame slot={} description={}", slot, description);

				DWORD exceptionCode = 0;
				const auto missionSaveFlag = ResolveMissionSaveFlag(ResolveNativeSaveGame());
				const bool saved = InvokeNativeSaveShellGame(
					saveShellGame, missionSaveFlag, slot, description.c_str(), exceptionCode);
				if (exceptionCode != 0)
				{
					LogNativeSave(
						"[EXU::SaveGame] native SaveShellGame crashed slot={} code=0x{:08X}",
						slot,
						exceptionCode
					);
					lua_pushboolean(L, 0);
					lua_pushfstring(L, "native SaveShellGame crashed with exception 0x%08X", exceptionCode);
					return 2;
				}

				LogNativeSave(
					"[EXU::SaveGame] native SaveShellGame result={} slot={} path={}",
					saved ? 1 : 0,
					slot,
					filename
				);

				lua_pushboolean(L, saved ? 1 : 0);
				if (saved)
				{
					lua_pushlstring(L, filename.c_str(), filename.size());
				}
				else
				{
					lua_pushfstring(L, "native SaveShellGame returned false for slot %d", slot);
				}

				return 2;
			}

			LogNativeSave(
				"[EXU::SaveGame] native SaveShellGame unavailable for slot={} description={}, falling back to SaveGame",
				slot,
				description
			);
		}

		const auto saveGame = ResolveNativeSaveGame();
		if (saveGame == nullptr)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "native SaveGame routine could not be resolved");
			return 2;
		}

		const auto missionSaveFlag = ResolveMissionSaveFlag(saveGame);
		if (missionSaveFlag == nullptr)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "native missionSave state could not be resolved");
			return 2;
		}

		const auto binarySave = ResolveBinarySaveState(missionSaveFlag);
		if (binarySave.saveFlag == nullptr)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "native binarysave state could not be resolved");
			return 2;
		}

		if (!EnsureSaveParentDirectory(filename))
		{
			LogNativeSave("[EXU::SaveGame] failed to create parent directory for {}", filename);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "failed to create parent directory for %s", filename.c_str());
			return 2;
		}

		LogNativeSave("[EXU::SaveGame] calling native save path={} type={}", filename, saveType);

		DWORD exceptionCode = 0;
		bool wroteBinary = false;
		const bool saved = InvokeNativeNormalSaveGame(
			saveGame,
			missionSaveFlag,
			binarySave,
			filename.data(),
			saveType,
			wroteBinary,
			exceptionCode);
		if (exceptionCode != 0)
		{
			LogNativeSave(
				"[EXU::SaveGame] native save crashed path={} type={} code=0x{:08X}",
				filename,
				saveType,
				exceptionCode
			);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "native save crashed with exception 0x%08X", exceptionCode);
			return 2;
		}

		LogNativeSave(
			"[EXU::SaveGame] native save result={} path={} type={} binary={}",
			saved ? 1 : 0, filename, saveType, wroteBinary ? 1 : 0);

		lua_pushboolean(L, saved ? 1 : 0);
		if (saved)
		{
			if (!description.empty() && wroteBinary)
			{
				LogNativeSave(
					"[EXU::SaveGame] description override skipped for binary save path={} description={}",
					filename,
					description
				);
			}
			else if (!description.empty() && !RewriteTextSaveDescription(filename, description))
			{
				LogNativeSave(
					"[EXU::SaveGame] description override could not be applied path={} description={}",
					filename,
					description
				);
			}

			lua_pushlstring(L, filename.c_str(), filename.size());
		}
		else
		{
			lua_pushfstring(L, "native SaveGame returned false for %s", filename.c_str());
		}

		return 2;
	}
}

#pragma pop_macro("MessageBox")
