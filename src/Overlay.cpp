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

#include "Overlay.h"

#include "Hook.h"
#include "LuaHelpers.h"
#include "Logging.h"
#include "OgreNativeFontBridge.h"
#include "Ogre.h"
#include "OgreOverlayShim.h"
#include "game_state.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		enum class ElementKind
		{
			Unknown,
			Panel,
			BorderPanel,
			TextArea,
		};

		std::unordered_map<std::string, ElementKind> knownElements;
		struct OverlayVisibilityState
		{
			bool requestedVisible = false;
			bool effectiveVisible = false;
		};
		std::unordered_map<std::string, OverlayVisibilityState> overlayVisibilityStates;
		std::unordered_set<void*> attachedOverlaySceneManagers;
		void* overlaySystemInstance = nullptr;
		std::unique_ptr<Hook> overlayPauseEnterHook;
		std::unique_ptr<Hook> overlayPauseExitHook;
		constexpr size_t kOverlaySystemAllocSize = 256;
		constexpr const char* kOverlayRuntimeResourceGroup = "EXUOverlayRuntime";
		constexpr const char* kOverlayRuntimeFontResourceGroup = "EXUOverlayFontRuntime";
		constexpr const char* kOverlayRuntimeFontName = "CRBZoneOverlayFont";
		constexpr const char* kOverlayRuntimeFontScript = "CRBZoneOverlay.fontdef";
		constexpr const char* kOverlayRuntimeTrueTypeSource = "BZONE.ttf";
		constexpr float kOverlayRuntimeTrueTypeSize = 32.0f;
		constexpr unsigned int kOverlayRuntimeTrueTypeResolution = 96u;
		constexpr unsigned int kOverlayRuntimeFirstCodePoint = 32u;
		constexpr unsigned int kOverlayRuntimeLastCodePoint = 126u;
		constexpr const char* kOverlayRuntimeFontSource = "bzfont.dds";
		constexpr const char* kOverlayRuntimeFontSpriteTable = "Edit\\stock\\bzfont.st";
		constexpr const char* kBattlezoneWorkshopAppId = "301650";
		bool overlayRuntimeResourcesReady = false;
		bool overlayRuntimeResourcesAttempted = false;
		bool overlayRuntimeFontReady = false;
		bool overlayRuntimeFontAttempted = false;
		std::string overlayRuntimeFontScriptPath;
		bool overlayPauseHooksAttempted = false;
		bool overlayPauseHooksReady = false;
		bool overlaySuppressionActive = false;
		volatile long overlayPauseWrapperDepth = 0;
		constexpr uintptr_t kPauseWrapperFunctionAddr = 0x005D4690;
		constexpr uintptr_t kPauseWrapperEntryHookOffset = 0x26;
		constexpr uintptr_t kPauseWrapperExitHookOffset = 0x1CA;
		constexpr std::array<uint8_t, 33> kPauseWrapperFunctionPattern = {
			0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x2C, 0x83, 0x91, 0x00, 0x00, 0x74, 0x05,
			0xE9, 0x05, 0x02, 0x00, 0x00, 0x0F, 0xB6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x85,
			0xC0, 0x0F, 0x85, 0xF6, 0x01, 0x00, 0x00
		};
		constexpr std::array<uint8_t, 33> kPauseWrapperFunctionMask = {
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 0, 0, 0, 0
		};
		constexpr std::array<uint8_t, 9> kPauseWrapperEntryHookBytes = {
			0x8B, 0x4D, 0x08, 0x51, 0x68, 0x64, 0x7A, 0x88, 0x00
		};
		constexpr std::array<uint8_t, 7> kPauseWrapperExitHookBytes = {
			0xC6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x00
		};

		void EnsureOverlayPauseHooksInstalled();

		ElementKind GetElementKindByTypeName(const std::string& typeName)
		{
			if (typeName == "Panel")
			{
				return ElementKind::Panel;
			}
			if (typeName == "BorderPanel")
			{
				return ElementKind::BorderPanel;
			}
			if (typeName == "TextArea")
			{
				return ElementKind::TextArea;
			}
			return ElementKind::Unknown;
		}

		bool IsContainerKind(ElementKind kind)
		{
			return kind == ElementKind::Panel || kind == ElementKind::BorderPanel;
		}

		using AddRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using RemoveRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using OverlaySystemCtorFn = void(__thiscall*)(void*);
		using OverlaySystemDtorFn = void(__thiscall*)(void*);

		HMODULE GetOgreOverlayModule()
		{
			static HMODULE ogreOverlay = GetModuleHandleA("OgreOverlay.dll");
			return ogreOverlay;
		}

		::Ogre::OverlayManager* GetOverlayManagerRaw()
		{
			__try
			{
				return ::Ogre::OverlayManager::getSingletonPtr();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		::Ogre::OverlayManager* GetOverlayManager();

		::Ogre::Overlay* FindOverlay(const std::string& name)
		{
			::Ogre::OverlayManager* manager = GetOverlayManager();
			if (manager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return manager->getByName(name);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		::Ogre::OverlayElement* FindOverlayElement(const std::string& name)
		{
			::Ogre::OverlayManager* manager = GetOverlayManager();
			if (manager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				if (!manager->hasOverlayElement(name, false))
				{
					return nullptr;
				}
				return manager->getOverlayElement(name, false);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		::Ogre::OverlayContainer* FindOverlayContainer(const std::string& name)
		{
			const auto it = knownElements.find(name);
			if (it == knownElements.end() || !IsContainerKind(it->second))
			{
				return nullptr;
			}

			return reinterpret_cast<::Ogre::OverlayContainer*>(FindOverlayElement(name));
		}

		bool IsFiniteColor(const ExtraUtilities::Ogre::Color& color)
		{
			return std::isfinite(color.r)
				&& std::isfinite(color.g)
				&& std::isfinite(color.b)
				&& std::isfinite(color.a);
		}

		const char* DescribeOptionalBool(bool available, bool value)
		{
			if (!available)
			{
				return "unavailable";
			}

			return value ? "true" : "false";
		}

		bool IsReadableRange(const void* address, size_t length) noexcept
		{
			if (address == nullptr || length == 0)
			{
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
			{
				return false;
			}

			if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || mbi.Protect == PAGE_NOACCESS)
			{
				return false;
			}

			const auto regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			const auto rangeBase = reinterpret_cast<uintptr_t>(address);
			if (rangeBase < regionBase)
			{
				return false;
			}

			const auto offset = rangeBase - regionBase;
			return offset <= mbi.RegionSize && length <= (mbi.RegionSize - offset);
		}

		bool TryGetMainModuleTextSection(const uint8_t*& outData, size_t& outSize, uintptr_t& outAddress)
		{
			outData = nullptr;
			outSize = 0;
			outAddress = 0;

			HMODULE module = GetModuleHandleA(nullptr);
			if (module == nullptr)
			{
				return false;
			}

			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (!IsReadableRange(dos, sizeof(IMAGE_DOS_HEADER)) || dos->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return false;
			}

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
			if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS)) || nt->Signature != IMAGE_NT_SIGNATURE)
			{
				return false;
			}

			const auto* section = IMAGE_FIRST_SECTION(nt);
			for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
			{
				if (std::strncmp(reinterpret_cast<const char*>(section->Name), ".text", IMAGE_SIZEOF_SHORT_NAME) != 0)
				{
					continue;
				}

				outData = reinterpret_cast<const uint8_t*>(module) + section->VirtualAddress;
				outSize = static_cast<size_t>(section->Misc.VirtualSize);
				outAddress = reinterpret_cast<uintptr_t>(outData);
				return outSize != 0;
			}

			return false;
		}

		uintptr_t FindMaskedPattern(
			const uint8_t* data,
			size_t dataSize,
			uintptr_t baseAddress,
			const uint8_t* pattern,
			const uint8_t* mask,
			size_t patternSize)
		{
			if (data == nullptr || pattern == nullptr || mask == nullptr || patternSize == 0 || dataSize < patternSize)
			{
				return 0;
			}

			for (size_t offset = 0; offset <= (dataSize - patternSize); ++offset)
			{
				bool matched = true;
				for (size_t i = 0; i < patternSize; ++i)
				{
					if (mask[i] != 0 && data[offset + i] != pattern[i])
					{
						matched = false;
						break;
					}
				}

				if (matched)
				{
					return baseAddress + offset;
				}
			}

			return 0;
		}

		template <size_t N>
		bool MatchBytes(uintptr_t address, const std::array<uint8_t, N>& bytes) noexcept
		{
			if (!IsReadableRange(reinterpret_cast<const void*>(address), N))
			{
				return false;
			}

			__try
			{
				return std::memcmp(reinterpret_cast<const void*>(address), bytes.data(), N) == 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool IsOverlaySuppressedByGameUi() noexcept
		{
			return overlayPauseWrapperDepth > 0 || ExtraUtilities::GameState::IsPauseMenuOpen();
		}

		using GetViewportOverlaysEnabledFn = bool(__thiscall*)(void*);
		using SetViewportOverlaysEnabledFn = void(__thiscall*)(void*, bool);
		using GetRootSingletonFn = void*(*)();
		using GetRootRenderSystemFn = void*(__thiscall*)(void*);
		using GetRenderSystemSharedListenerFn = void*(*)();
		using GetRenderSystemViewportFn = void*(__thiscall*)(void*);
		HMODULE GetOgreMainModule()
		{
			static HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
			return ogreMain;
		}

		GetViewportOverlaysEnabledFn ResolveGetViewportOverlaysEnabled()
		{
			static GetViewportOverlaysEnabledFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetViewportOverlaysEnabledFn>(nullptr);
				}

				return reinterpret_cast<GetViewportOverlaysEnabledFn>(
					GetProcAddress(ogreMain, "?getOverlaysEnabled@Viewport@Ogre@@QBE_NXZ"));
			}();

			return fn;
		}

		SetViewportOverlaysEnabledFn ResolveSetViewportOverlaysEnabled()
		{
			static SetViewportOverlaysEnabledFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<SetViewportOverlaysEnabledFn>(nullptr);
				}

				return reinterpret_cast<SetViewportOverlaysEnabledFn>(
					GetProcAddress(ogreMain, "?setOverlaysEnabled@Viewport@Ogre@@QAEX_N@Z"));
			}();

			return fn;
		}

		GetRootSingletonFn ResolveGetRootSingleton()
		{
			static GetRootSingletonFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRootSingletonFn>(nullptr);
				}

				return reinterpret_cast<GetRootSingletonFn>(
					GetProcAddress(ogreMain, "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ"));
			}();

			return fn;
		}

		GetRootRenderSystemFn ResolveGetRootRenderSystem()
		{
			static GetRootRenderSystemFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRootRenderSystemFn>(nullptr);
				}

				return reinterpret_cast<GetRootRenderSystemFn>(
					GetProcAddress(ogreMain, "?getRenderSystem@Root@Ogre@@QAEPAVRenderSystem@2@XZ"));
			}();

			return fn;
		}

		GetRenderSystemSharedListenerFn ResolveGetRenderSystemSharedListener()
		{
			static GetRenderSystemSharedListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRenderSystemSharedListenerFn>(nullptr);
				}

				return reinterpret_cast<GetRenderSystemSharedListenerFn>(
					GetProcAddress(ogreMain, "?getSharedListener@RenderSystem@Ogre@@SAPAVListener@12@XZ"));
			}();

			return fn;
		}

		GetRenderSystemViewportFn ResolveGetRenderSystemViewport()
		{
			static GetRenderSystemViewportFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRenderSystemViewportFn>(nullptr);
				}

				return reinterpret_cast<GetRenderSystemViewportFn>(
					GetProcAddress(ogreMain, "?_getViewport@RenderSystem@Ogre@@UAEPAVViewport@2@XZ"));
			}();

			return fn;
		}

		bool TryGetRootSingleton(void*& outRoot)
		{
			outRoot = nullptr;
			const auto fn = ResolveGetRootSingleton();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRoot = fn();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] Root::getSingletonPtr crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		std::string GetDirectoryForModule(HMODULE module)
		{
			if (module == nullptr)
			{
				return {};
			}

			std::array<char, MAX_PATH> path{};
			const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size())
			{
				return {};
			}

			std::string result(path.data(), length);
			const auto slash = result.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				return {};
			}

			result.resize(slash);
			return result;
		}

		std::string GetCurrentModuleDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&GetCurrentModuleDirectory),
				&module) || module == nullptr)
			{
				return {};
			}

			return GetDirectoryForModule(module);
		}

		std::string GetCurrentGameRootDirectory()
		{
			return GetDirectoryForModule(GetModuleHandleA(nullptr));
		}

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path, error);
		}

		bool IsDirectory(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_directory(path, error);
		}

		void AppendUniquePath(std::vector<std::filesystem::path>& paths, std::unordered_set<std::string>& seen, const std::filesystem::path& path)
		{
			if (path.empty())
			{
				return;
			}

			std::error_code error;
			const std::filesystem::path normalized = path.lexically_normal();
			const std::string key = normalized.string();
			if (key.empty() || !seen.insert(key).second)
			{
				return;
			}

			paths.push_back(normalized);
		}

		bool ContainsOverlayRuntimeFontAsset(const std::filesystem::path& directory)
		{
			if (!IsDirectory(directory))
			{
				return false;
			}

			return IsRegularFile(directory / kOverlayRuntimeFontScript)
				|| IsRegularFile(directory / kOverlayRuntimeTrueTypeSource);
		}

		void AppendOverlayFontCandidatesForBase(
			const std::filesystem::path& base,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			if (base.empty())
			{
				return;
			}

			AppendUniquePath(candidates, seen, base / "OverlayFont");
			AppendUniquePath(candidates, seen, base);
		}

		void AppendNearbyOverlayFontCandidates(
			const std::filesystem::path& start,
			size_t maxAncestorDepth,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			std::filesystem::path current = start;
			for (size_t depth = 0; !current.empty() && depth <= maxAncestorDepth; ++depth)
			{
				AppendOverlayFontCandidatesForBase(current, candidates, seen);

				const std::filesystem::path parent = current.parent_path();
				if (parent.empty() || parent == current)
				{
					break;
				}

				current = parent;
			}
		}

		void AppendOverlayFontCandidatesUnder(
			const std::filesystem::path& root,
			size_t maxDepth,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			if (!IsDirectory(root))
			{
				return;
			}

			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
				it != end;
				it.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}

				if (it.depth() > static_cast<int>(maxDepth))
				{
					it.disable_recursion_pending();
					continue;
				}

				if (!it->is_directory(error))
				{
					error.clear();
					continue;
				}

				AppendUniquePath(candidates, seen, it->path());
			}
		}

		std::filesystem::path GetWorkshopContentDirectory(const std::filesystem::path& gameRootDirectory)
		{
			if (gameRootDirectory.empty())
			{
				return {};
			}

			const std::filesystem::path commonDirectory = gameRootDirectory.parent_path();
			if (commonDirectory.empty())
			{
				return {};
			}

			const std::filesystem::path steamappsDirectory = commonDirectory.parent_path();
			if (steamappsDirectory.empty())
			{
				return {};
			}

			return steamappsDirectory / "workshop" / "content" / kBattlezoneWorkshopAppId;
		}

		AddRenderQueueListenerFn ResolveAddRenderQueueListener()
		{
			static AddRenderQueueListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<AddRenderQueueListenerFn>(nullptr);
				}

				return reinterpret_cast<AddRenderQueueListenerFn>(
					GetProcAddress(ogreMain, "?addRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z"));
			}();

			return fn;
		}

		bool TryAddRenderQueueListenerWithSeh(void* sceneManager, void* overlaySystem, AddRenderQueueListenerFn addListener)
		{
			__try
			{
				addListener(sceneManager, overlaySystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] addRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, GetExceptionCode());
				return false;
			}
		}

		RemoveRenderQueueListenerFn ResolveRemoveRenderQueueListener()
		{
			static RemoveRenderQueueListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<RemoveRenderQueueListenerFn>(nullptr);
				}

				return reinterpret_cast<RemoveRenderQueueListenerFn>(
					GetProcAddress(ogreMain, "?removeRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z"));
			}();

			return fn;
		}

		OverlaySystemCtorFn ResolveOverlaySystemCtor()
		{
			static OverlaySystemCtorFn fn = []()
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return static_cast<OverlaySystemCtorFn>(nullptr);
				}

				return reinterpret_cast<OverlaySystemCtorFn>(
					GetProcAddress(ogreOverlay, "??0OverlaySystem@Ogre@@QAE@XZ"));
			}();

			return fn;
		}

		OverlaySystemDtorFn ResolveOverlaySystemDtor()
		{
			static OverlaySystemDtorFn fn = []()
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return static_cast<OverlaySystemDtorFn>(nullptr);
				}

				return reinterpret_cast<OverlaySystemDtorFn>(
					GetProcAddress(ogreOverlay, "??1OverlaySystem@Ogre@@UAE@XZ"));
			}();

			return fn;
		}

		void* GetSceneManagerForOverlay()
		{
			void* sceneManager = nullptr;
			__try
			{
				sceneManager = ExtraUtilities::Ogre::sceneManager.Read();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] scene manager lookup crashed code=0x%08X", GetExceptionCode());
				return nullptr;
			}

			return sceneManager;
		}

		bool TryConstructOverlaySystem(void*& outOverlaySystem)
		{
			outOverlaySystem = nullptr;
			const auto ctor = ResolveOverlaySystemCtor();
			if (ctor == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem ctor unavailable");
				return false;
			}

			void* storage = std::malloc(kOverlaySystemAllocSize);
			if (storage == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem allocation failed size=%u", static_cast<unsigned>(kOverlaySystemAllocSize));
				return false;
			}

			std::memset(storage, 0, kOverlaySystemAllocSize);
			__try
			{
				ctor(storage);
				outOverlaySystem = storage;
				Logging::LogMessage("[EXU::Overlay] OverlaySystem constructed instance=%p", outOverlaySystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem ctor crashed storage=%p code=0x%08X", storage, GetExceptionCode());
				std::free(storage);
				return false;
			}
		}

		bool TryAttachOverlaySystemToSceneManager(void* sceneManager, void* overlaySystem)
		{
			if (sceneManager == nullptr || overlaySystem == nullptr)
			{
				return false;
			}

			if (attachedOverlaySceneManagers.find(sceneManager) != attachedOverlaySceneManagers.end())
			{
				return true;
			}

			const auto addListener = ResolveAddRenderQueueListener();
			if (addListener == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] addRenderQueueListener unavailable sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				return false;
			}

			if (!TryAddRenderQueueListenerWithSeh(sceneManager, overlaySystem, addListener))
			{
				return false;
			}

			attachedOverlaySceneManagers.insert(sceneManager);
			Logging::LogMessage("[EXU::Overlay] OverlaySystem attached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
			return true;
		}

		void EnsureOverlaySupport()
		{
			if (GetOverlayManagerRaw() == nullptr && overlaySystemInstance == nullptr)
			{
				TryConstructOverlaySystem(overlaySystemInstance);
			}

			void* sceneManager = GetSceneManagerForOverlay();
			if (sceneManager != nullptr && overlaySystemInstance != nullptr)
			{
				TryAttachOverlaySystemToSceneManager(sceneManager, overlaySystemInstance);
			}
		}

		void EnsureOverlayRuntimeResources()
		{
			if (overlayRuntimeResourcesReady || overlayRuntimeResourcesAttempted)
			{
				return;
			}

			overlayRuntimeResourcesAttempted = true;
			EnsureOverlaySupport();

			const std::string moduleDirectory = GetCurrentModuleDirectory();
			if (moduleDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve EXU module directory");
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve game root directory");
				return;
			}

			const std::filesystem::path moduleDirectoryPath(moduleDirectory);
			const std::filesystem::path gameRootDirectoryPath(gameRootDirectory);
			std::vector<std::filesystem::path> fontDirectories;
			std::unordered_set<std::string> seenFontDirectories;
			std::vector<std::string> addedFontDirectories;

			AppendNearbyOverlayFontCandidates(moduleDirectoryPath, 5, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesForBase(gameRootDirectoryPath, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "addon", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "mods", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "packaged_mods", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(GetWorkshopContentDirectory(gameRootDirectoryPath), 3, fontDirectories, seenFontDirectories);

			bool addedAnyFontLocation = false;
			for (const std::filesystem::path& fontDirectory : fontDirectories)
			{
				if (!ContainsOverlayRuntimeFontAsset(fontDirectory))
				{
					continue;
				}

				const std::string fontDirectoryString = fontDirectory.string();
				const std::filesystem::path fontScriptPath = fontDirectory / kOverlayRuntimeFontScript;
				if (IsRegularFile(fontScriptPath))
				{
					const bool currentIsOverlayFont = !overlayRuntimeFontScriptPath.empty()
						&& std::filesystem::path(overlayRuntimeFontScriptPath).parent_path().filename() == "OverlayFont";
					const bool candidateIsOverlayFont = fontDirectory.filename() == "OverlayFont";
					if (overlayRuntimeFontScriptPath.empty() || (candidateIsOverlayFont && !currentIsOverlayFont))
					{
						overlayRuntimeFontScriptPath = fontScriptPath.string();
					}
				}
				if (Native::TryAddResourceLocation(fontDirectoryString.c_str(), kOverlayRuntimeResourceGroup))
				{
					addedAnyFontLocation = true;
					addedFontDirectories.push_back(fontDirectoryString);
				}
			}

			if (!addedAnyFontLocation)
			{
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime resources failed to locate any font directory module=%s gameRoot=%s",
					moduleDirectory.c_str(),
					gameRootDirectory.c_str());
				return;
			}

			const std::string stockTextureDirectory = gameRootDirectory + "\\BZ_ASSETS\\pc\\textures\\MISC_DDS";
			if (!Native::TryAddResourceLocation(stockTextureDirectory.c_str(), kOverlayRuntimeResourceGroup))
			{
				return;
			}

			overlayRuntimeResourcesReady = true;
			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime resources ready group=%s moduleDir=%s gameRoot=%s stockTextures=%s fontLocationCount=%zu",
				kOverlayRuntimeResourceGroup,
				moduleDirectory.c_str(),
				gameRootDirectory.c_str(),
				stockTextureDirectory.c_str(),
				addedFontDirectories.size());
			for (const std::string& fontDirectory : addedFontDirectories)
			{
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font location group=%s path=%s",
					kOverlayRuntimeResourceGroup,
					fontDirectory.c_str());
			}
		}

		void EnsureOverlayRuntimeFont()
		{
			if (overlayRuntimeFontReady || overlayRuntimeFontAttempted)
			{
				return;
			}

			overlayRuntimeFontAttempted = true;
			EnsureOverlayRuntimeResources();
			if (!overlayRuntimeResourcesReady)
			{
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime font failed to resolve game root directory");
				return;
			}

			if (Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeResourceGroup))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s mode=existing",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup);
				return;
			}

			if (!overlayRuntimeFontScriptPath.empty())
			{
				const std::filesystem::path fontScriptPath(overlayRuntimeFontScriptPath);
				const std::string fontScriptDirectory = fontScriptPath.parent_path().string();
				const std::string stockTextureDirectory = gameRootDirectory + "\\BZ_ASSETS\\pc\\textures\\MISC_DDS";
				if (!fontScriptDirectory.empty()
					&& Native::TryAddResourceLocation(fontScriptDirectory.c_str(), kOverlayRuntimeFontResourceGroup)
					&& Native::TryAddResourceLocation(stockTextureDirectory.c_str(), kOverlayRuntimeFontResourceGroup)
					&& Native::TryParseFontScript(kOverlayRuntimeFontScript, kOverlayRuntimeFontResourceGroup)
					&& Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeFontResourceGroup))
				{
					overlayRuntimeFontReady = true;
					Logging::LogMessage(
						"[EXU::Overlay] overlay runtime font ready name=%s group=%s script=%s mode=script",
						kOverlayRuntimeFontName,
						kOverlayRuntimeFontResourceGroup,
						overlayRuntimeFontScriptPath.c_str());
					return;
				}
			}

			if (Native::TryEnsureTrueTypeFont(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeTrueTypeSize,
				kOverlayRuntimeTrueTypeResolution,
				kOverlayRuntimeFirstCodePoint,
				kOverlayRuntimeLastCodePoint))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s mode=truetype size=%.1f resolution=%u range=%u-%u",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeTrueTypeSource,
					kOverlayRuntimeTrueTypeSize,
					kOverlayRuntimeTrueTypeResolution,
					kOverlayRuntimeFirstCodePoint,
					kOverlayRuntimeLastCodePoint);
				return;
			}

			const std::string spriteTablePath = gameRootDirectory + "\\" + kOverlayRuntimeFontSpriteTable;
			if (Native::TryEnsureImageFontFromSpriteTable(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str()))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s spriteTable=%s mode=image-fallback",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeFontSource,
					spriteTablePath.c_str());
				return;
			}

			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime font unavailable name=%s group=%s truetype=%s image=%s spriteTable=%s",
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str());
		}

		void DetachOverlaySystemFromTrackedSceneManagers(void* overlaySystem)
		{
			if (overlaySystem == nullptr || attachedOverlaySceneManagers.empty())
			{
				return;
			}

			const auto removeListener = ResolveRemoveRenderQueueListener();
			if (removeListener == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener unavailable overlaySystem=%p", overlaySystem);
				return;
			}

			for (void* sceneManager : attachedOverlaySceneManagers)
			{
				if (sceneManager == nullptr)
				{
					continue;
				}

				__try
				{
					removeListener(sceneManager, overlaySystem);
					Logging::LogMessage("[EXU::Overlay] OverlaySystem detached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, GetExceptionCode());
				}
			}

			attachedOverlaySceneManagers.clear();
		}

		void DestroyOverlaySystemInstance()
		{
			if (overlaySystemInstance == nullptr)
			{
				return;
			}

			const auto dtor = ResolveOverlaySystemDtor();
			if (dtor == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem dtor unavailable instance=%p", overlaySystemInstance);
				return;
			}

			void* instance = overlaySystemInstance;
			overlaySystemInstance = nullptr;

			__try
			{
				dtor(instance);
				Logging::LogMessage("[EXU::Overlay] OverlaySystem destroyed instance=%p", instance);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem dtor crashed instance=%p code=0x%08X", instance, GetExceptionCode());
			}

			std::free(instance);
		}

		::Ogre::OverlayManager* GetOverlayManager()
		{
			EnsureOverlayPauseHooksInstalled();
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			if (manager != nullptr)
			{
				return manager;
			}

			EnsureOverlaySupport();
			return GetOverlayManagerRaw();
		}

		bool TryGetRootRenderSystem(void* root, void*& outRenderSystem)
		{
			outRenderSystem = nullptr;
			if (root == nullptr)
			{
				return false;
			}

			const auto fn = ResolveGetRootRenderSystem();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRenderSystem = fn(root);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] Root::getRenderSystem crashed root=%p code=0x%08X", root, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemSharedListener(void*& outSharedListener)
		{
			outSharedListener = nullptr;
			const auto fn = ResolveGetRenderSystemSharedListener();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outSharedListener = fn();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] RenderSystem::getSharedListener crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemViewport(void* renderSystem, void*& outViewport)
		{
			outViewport = nullptr;
			if (renderSystem == nullptr)
			{
				return false;
			}

			const auto fn = ResolveGetRenderSystemViewport();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outViewport = fn(renderSystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] RenderSystem::_getViewport crashed renderSystem=%p code=0x%08X", renderSystem, GetExceptionCode());
				return false;
			}
		}

		void* GetCurrentViewportForOverlay()
		{
			void* sceneManager = GetSceneManagerForOverlay();
			if (sceneManager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return ExtraUtilities::Ogre::GetCurrentViewport(sceneManager);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] current viewport lookup crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return nullptr;
			}
		}

		bool TryGetViewportOverlaysEnabled(void* viewport, bool& outEnabled)
		{
			outEnabled = false;
			if (viewport == nullptr)
			{
				return false;
			}

			const auto fn = ResolveGetViewportOverlaysEnabled();
			if (fn == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] getOverlaysEnabled unavailable viewport=%p", viewport);
				return false;
			}

			__try
			{
				outEnabled = fn(viewport);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] getOverlaysEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
				outEnabled = false;
				return false;
			}
		}

		bool TrySetViewportOverlaysEnabled(void* viewport, bool enabled)
		{
			if (viewport == nullptr)
			{
				return false;
			}

			const auto fn = ResolveSetViewportOverlaysEnabled();
			if (fn == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] setOverlaysEnabled unavailable viewport=%p enabled=%d", viewport, enabled ? 1 : 0);
				return false;
			}

			__try
			{
				fn(viewport, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] setOverlaysEnabled crashed viewport=%p enabled=%d code=0x%08X", viewport, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TryCallSetOverlayParameter(
			bool(__thiscall* setParameter)(void*, const std::string&, const std::string&),
			::Ogre::OverlayElement* element,
			const std::string& name,
			const std::string& value,
			bool& outSuccess,
			unsigned int& outExceptionCode)
		{
			outSuccess = false;
			outExceptionCode = 0;

			__try
			{
				outSuccess = setParameter(element, name, value);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryShowOverlay(::Ogre::Overlay* overlay, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;

			__try
			{
				overlay->show();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryHideOverlay(::Ogre::Overlay* overlay, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;

			__try
			{
				overlay->hide();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		void SyncOverlayVisibilityState(const std::string& name, OverlayVisibilityState& visibilityState, const char* reason)
		{
			const bool shouldBeVisible = visibilityState.requestedVisible && !overlaySuppressionActive;
			if (visibilityState.effectiveVisible == shouldBeVisible)
			{
				return;
			}

			::Ogre::Overlay* overlay = FindOverlay(name);
			if (overlay == nullptr)
			{
				visibilityState.effectiveVisible = false;
				return;
			}

			unsigned int exceptionCode = 0;
			const bool success = shouldBeVisible
				? TryShowOverlay(overlay, exceptionCode)
				: TryHideOverlay(overlay, exceptionCode);

			if (!success)
			{
				Logging::LogMessage(
					"[EXU::Overlay] SyncOverlayVisibility failed name=%s reason=%s targetVisible=%d code=0x%08X",
					name.c_str(),
					reason != nullptr ? reason : "unknown",
					shouldBeVisible ? 1 : 0,
					exceptionCode);
				return;
			}

			visibilityState.effectiveVisible = shouldBeVisible;
			Logging::LogMessage(
				"[EXU::Overlay] SyncOverlayVisibility name=%s reason=%s requested=%d effective=%d suppressed=%d",
				name.c_str(),
				reason != nullptr ? reason : "unknown",
				visibilityState.requestedVisible ? 1 : 0,
				visibilityState.effectiveVisible ? 1 : 0,
				overlaySuppressionActive ? 1 : 0);
		}

		void RefreshOverlaySuppressionState(const char* reason)
		{
			const bool shouldSuppress = IsOverlaySuppressedByGameUi();
			if (overlaySuppressionActive != shouldSuppress)
			{
				Logging::LogMessage(
					"[EXU::Overlay] Suppression state changed reason=%s suppressed=%d depth=%ld pauseProbe=%d tracked=%u",
					reason != nullptr ? reason : "unknown",
					shouldSuppress ? 1 : 0,
					overlayPauseWrapperDepth,
					ExtraUtilities::GameState::IsPauseMenuOpen() ? 1 : 0,
					static_cast<unsigned>(overlayVisibilityStates.size()));
			}

			overlaySuppressionActive = shouldSuppress;
			if (overlayVisibilityStates.empty())
			{
				return;
			}

			for (auto& [name, visibilityState] : overlayVisibilityStates)
			{
				SyncOverlayVisibilityState(name, visibilityState, reason);
			}
		}

		void OnOverlayPauseWrapperEnter(int dialogId)
		{
			const long depth = InterlockedIncrement(&overlayPauseWrapperDepth);
			Logging::LogMessage("[EXU::Overlay] pause wrapper enter dialog=%d depth=%ld", dialogId, depth);
			RefreshOverlaySuppressionState("pause-wrapper-enter");
		}

		void OnOverlayPauseWrapperExit()
		{
			long depth = InterlockedDecrement(&overlayPauseWrapperDepth);
			if (depth < 0)
			{
				overlayPauseWrapperDepth = 0;
				depth = 0;
			}

			Logging::LogMessage("[EXU::Overlay] pause wrapper exit depth=%ld", depth);
			RefreshOverlaySuppressionState("pause-wrapper-exit");
		}

		static void __declspec(naked) OverlayPauseWrapperEnterHook()
		{
			__asm
			{
				pushad
				pushfd

				mov eax, [ebp+0x08]
				push eax
				call OnOverlayPauseWrapperEnter
				add esp, 0x04

				popfd
				popad

				pop eax
				mov ecx, [ebp+0x08]
				push ecx
				push 0x00887A64
				jmp eax
			}
		}

		static void __declspec(naked) OverlayPauseWrapperExitHook()
		{
			__asm
			{
				mov byte ptr ds:[0x0091812B], 0

				pushad
				pushfd

				call OnOverlayPauseWrapperExit

				popfd
				popad
				ret
			}
		}

		void DestroyOverlayPauseHooks()
		{
			overlayPauseExitHook.reset();
			overlayPauseEnterHook.reset();
			overlayPauseHooksReady = false;
			overlayPauseHooksAttempted = false;
			overlayPauseWrapperDepth = 0;
			overlaySuppressionActive = false;
		}

		uintptr_t ResolvePauseWrapperFunctionAddress()
		{
			if (MatchBytes(kPauseWrapperFunctionAddr, kPauseWrapperFunctionPattern))
			{
				return kPauseWrapperFunctionAddr;
			}

			const uint8_t* textData = nullptr;
			size_t textSize = 0;
			uintptr_t textAddress = 0;
			if (!TryGetMainModuleTextSection(textData, textSize, textAddress))
			{
				return 0;
			}

			return FindMaskedPattern(
				textData,
				textSize,
				textAddress,
				kPauseWrapperFunctionPattern.data(),
				kPauseWrapperFunctionMask.data(),
				kPauseWrapperFunctionPattern.size());
		}

		void EnsureOverlayPauseHooksInstalled()
		{
			if (overlayPauseHooksAttempted)
			{
				return;
			}

			overlayPauseHooksAttempted = true;
			const uintptr_t functionAddress = ResolvePauseWrapperFunctionAddress();
			if (functionAddress == 0)
			{
				Logging::LogMessage("[EXU::Overlay] pause wrapper hook install failed reason=function-not-found");
				return;
			}

			const uintptr_t entryHookAddress = functionAddress + kPauseWrapperEntryHookOffset;
			const uintptr_t exitHookAddress = functionAddress + kPauseWrapperExitHookOffset;
			if (!MatchBytes(entryHookAddress, kPauseWrapperEntryHookBytes)
				|| !MatchBytes(exitHookAddress, kPauseWrapperExitHookBytes))
			{
				Logging::LogMessage(
					"[EXU::Overlay] pause wrapper hook install failed reason=byte-mismatch function=%p entry=%p exit=%p",
					reinterpret_cast<void*>(functionAddress),
					reinterpret_cast<void*>(entryHookAddress),
					reinterpret_cast<void*>(exitHookAddress));
				return;
			}

			overlayPauseEnterHook = std::make_unique<Hook>(
				entryHookAddress,
				&OverlayPauseWrapperEnterHook,
				kPauseWrapperEntryHookBytes.size(),
				BasicPatch::Status::ACTIVE);
			overlayPauseExitHook = std::make_unique<Hook>(
				exitHookAddress,
				&OverlayPauseWrapperExitHook,
				kPauseWrapperExitHookBytes.size(),
				BasicPatch::Status::ACTIVE);

			overlayPauseHooksReady = overlayPauseEnterHook != nullptr
				&& overlayPauseEnterHook->IsActive()
				&& overlayPauseExitHook != nullptr
				&& overlayPauseExitHook->IsActive();
			Logging::LogMessage(
				"[EXU::Overlay] pause wrapper hooks %s function=%p entry=%p exit=%p",
				overlayPauseHooksReady ? "ready" : "inactive",
				reinterpret_cast<void*>(functionAddress),
				reinterpret_cast<void*>(entryHookAddress),
				reinterpret_cast<void*>(exitHookAddress));
		}

		std::string ToLowerCopy(const std::string& value)
		{
			std::string lowered(value);
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch)
				{
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool TryParseFloatList(const std::string& value, std::vector<float>& outValues)
		{
			outValues.clear();
			const char* cursor = value.c_str();
			const char* end = cursor + value.size();
			while (cursor < end)
			{
				while (cursor < end && std::isspace(static_cast<unsigned char>(*cursor)))
				{
					++cursor;
				}

				if (cursor >= end)
				{
					break;
				}

				errno = 0;
				char* parseEnd = nullptr;
				const float parsed = std::strtof(cursor, &parseEnd);
				if (parseEnd == cursor || errno == ERANGE || !std::isfinite(parsed))
				{
					outValues.clear();
					return false;
				}

				outValues.push_back(parsed);
				cursor = parseEnd;
			}

			return !outValues.empty();
		}

		bool TryParseBoolValue(const std::string& value, bool& outValue)
		{
			const std::string lowered = ToLowerCopy(value);
			if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
			{
				outValue = true;
				return true;
			}

			if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
			{
				outValue = false;
				return true;
			}

			return false;
		}

		bool TryParseTextAlignment(
			const std::string& value,
			::Ogre::TextAreaOverlayElement::Alignment& outAlignment)
		{
			const std::string lowered = ToLowerCopy(value);
			if (lowered == "left")
			{
				outAlignment = ::Ogre::TextAreaOverlayElement::Left;
				return true;
			}

			if (lowered == "right")
			{
				outAlignment = ::Ogre::TextAreaOverlayElement::Right;
				return true;
			}

			if (lowered == "center" || lowered == "centre")
			{
				outAlignment = ::Ogre::TextAreaOverlayElement::Center;
				return true;
			}

			return false;
		}

		bool TryParseColourValue(const std::string& value, ::Ogre::ColourValue& outColor)
		{
			std::vector<float> components;
			if (!TryParseFloatList(value, components) || (components.size() != 3 && components.size() != 4))
			{
				return false;
			}

			const float alpha = components.size() == 4 ? components[3] : 1.0f;
			outColor = ::Ogre::ColourValue(components[0], components[1], components[2], alpha);
			return true;
		}

		ElementKind GetKnownElementKind(const std::string& elementName)
		{
			const auto it = knownElements.find(elementName);
			return it != knownElements.end() ? it->second : ElementKind::Unknown;
		}

		bool TryCallPanelSetTransparent(::Ogre::PanelOverlayElement* element, bool transparent, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, bool);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setTransparent@PanelOverlayElement@Ogre@@QAEX_N@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, transparent);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallPanelSetTiling(::Ogre::PanelOverlayElement* element, float x, float y, unsigned short layer, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float, float, unsigned short);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setTiling@PanelOverlayElement@Ogre@@QAEXMMG@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, x, y, layer);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallPanelSetUV(::Ogre::PanelOverlayElement* element, float u1, float v1, float u2, float v2, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float, float, float, float);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setUV@PanelOverlayElement@Ogre@@QAEXMMMM@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, u1, v1, u2, v2);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallBorderPanelSetBorderSize1(::Ogre::BorderPanelOverlayElement* element, float size, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setBorderSize@BorderPanelOverlayElement@Ogre@@QAEXM@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, size);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallBorderPanelSetBorderSize2(::Ogre::BorderPanelOverlayElement* element, float sides, float topBottom, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float, float);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setBorderSize@BorderPanelOverlayElement@Ogre@@QAEXMM@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, sides, topBottom);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallBorderPanelSetBorderSize4(
			::Ogre::BorderPanelOverlayElement* element,
			float left,
			float right,
			float top,
			float bottom,
			unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float, float, float, float);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setBorderSize@BorderPanelOverlayElement@Ogre@@QAEXMMMM@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, left, right, top, bottom);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallBorderPanelSetBorderMaterial(
			::Ogre::BorderPanelOverlayElement* element,
			const std::string& materialName,
			unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, const std::string&);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setBorderMaterialName@BorderPanelOverlayElement@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, materialName);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallTextAreaSetAlignment(
			::Ogre::TextAreaOverlayElement* element,
			::Ogre::TextAreaOverlayElement::Alignment alignment,
			unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, ::Ogre::TextAreaOverlayElement::Alignment);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setAlignment@TextAreaOverlayElement@Ogre@@QAEXW4Alignment@12@@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, alignment);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallTextAreaSetSpaceWidth(::Ogre::TextAreaOverlayElement* element, float width, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, float);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setSpaceWidth@TextAreaOverlayElement@Ogre@@QAEXM@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, width);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallTextAreaSetColourTop(
			::Ogre::TextAreaOverlayElement* element,
			const ::Ogre::ColourValue& color,
			unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, const ::Ogre::ColourValue&);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setColourTop@TextAreaOverlayElement@Ogre@@QAEXABVColourValue@2@@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, color);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryCallTextAreaSetColourBottom(
			::Ogre::TextAreaOverlayElement* element,
			const ::Ogre::ColourValue& color,
			unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;
			using Fn = void(__thiscall*)(void*, const ::Ogre::ColourValue&);
			static Fn fn = nullptr;
			if (fn == nullptr)
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return false;
				}

				fn = reinterpret_cast<Fn>(
					GetProcAddress(ogreOverlay, "?setColourBottom@TextAreaOverlayElement@Ogre@@QAEXABVColourValue@2@@Z"));
			}

			if (fn == nullptr || element == nullptr)
			{
				return false;
			}

			__try
			{
				fn(element, color);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TrySetOverlayParameterDirect(
			const std::string& elementName,
			::Ogre::OverlayElement* element,
			const std::string& name,
			const std::string& value,
			bool& outHandled)
		{
			outHandled = false;
			if (element == nullptr)
			{
				return false;
			}

			const ElementKind kind = GetKnownElementKind(elementName);
			const std::string normalizedName = ToLowerCopy(name);
			if (normalizedName == "transparent")
			{
				outHandled = true;
				if (kind != ElementKind::Panel && kind != ElementKind::BorderPanel)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				bool transparent = false;
				if (!TryParseBoolValue(value, transparent))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-bool", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				if (!TryCallPanelSetTransparent(reinterpret_cast<::Ogre::PanelOverlayElement*>(element), transparent, exceptionCode))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			if (normalizedName == "alignment")
			{
				outHandled = true;
				if (kind != ElementKind::TextArea)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				::Ogre::TextAreaOverlayElement::Alignment alignment{};
				if (!TryParseTextAlignment(value, alignment))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-alignment", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				if (!TryCallTextAreaSetAlignment(reinterpret_cast<::Ogre::TextAreaOverlayElement*>(element), alignment, exceptionCode))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			if (normalizedName == "space_width")
			{
				outHandled = true;
				if (kind != ElementKind::TextArea)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				std::vector<float> values;
				if (!TryParseFloatList(value, values) || values.size() != 1)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-float", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				if (!TryCallTextAreaSetSpaceWidth(reinterpret_cast<::Ogre::TextAreaOverlayElement*>(element), values[0], exceptionCode))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			if (normalizedName == "colour_top" || normalizedName == "colour_bottom")
			{
				outHandled = true;
				if (kind != ElementKind::TextArea)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				::Ogre::ColourValue color;
				if (!TryParseColourValue(value, color))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-color", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				const bool success = normalizedName == "colour_top"
					? TryCallTextAreaSetColourTop(reinterpret_cast<::Ogre::TextAreaOverlayElement*>(element), color, exceptionCode)
					: TryCallTextAreaSetColourBottom(reinterpret_cast<::Ogre::TextAreaOverlayElement*>(element), color, exceptionCode);
				if (!success)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			if (normalizedName == "tiling" || normalizedName == "uv_coords")
			{
				outHandled = true;
				if (kind != ElementKind::Panel && kind != ElementKind::BorderPanel)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				std::vector<float> values;
				if (!TryParseFloatList(value, values))
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-float-list", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				bool success = false;
				if (normalizedName == "tiling")
				{
					if (values.size() != 2 && values.size() != 3)
					{
						Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-tiling-arity", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
						return false;
					}

					unsigned short layer = 0;
					if (values.size() == 3)
					{
						const float layerFloat = values[2];
						if (layerFloat < 0.0f || layerFloat > 65535.0f || std::floor(layerFloat) != layerFloat)
						{
							Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-layer", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
							return false;
						}

						layer = static_cast<unsigned short>(layerFloat);
					}

					success = TryCallPanelSetTiling(reinterpret_cast<::Ogre::PanelOverlayElement*>(element), values[0], values[1], layer, exceptionCode);
				}
				else
				{
					if (values.size() != 4)
					{
						Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-uv-arity", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
						return false;
					}

					success = TryCallPanelSetUV(reinterpret_cast<::Ogre::PanelOverlayElement*>(element), values[0], values[1], values[2], values[3], exceptionCode);
				}

				if (!success)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			if (normalizedName == "border_size" || normalizedName == "border_material")
			{
				outHandled = true;
				if (kind != ElementKind::BorderPanel)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=type-mismatch", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
					return false;
				}

				unsigned int exceptionCode = 0;
				bool success = false;
				if (normalizedName == "border_material")
				{
					success = TryCallBorderPanelSetBorderMaterial(reinterpret_cast<::Ogre::BorderPanelOverlayElement*>(element), value, exceptionCode);
				}
				else
				{
					std::vector<float> values;
					if (!TryParseFloatList(value, values))
					{
						Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-float-list", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
						return false;
					}

					switch (values.size())
					{
					case 1:
						success = TryCallBorderPanelSetBorderSize1(reinterpret_cast<::Ogre::BorderPanelOverlayElement*>(element), values[0], exceptionCode);
						break;
					case 2:
						success = TryCallBorderPanelSetBorderSize2(reinterpret_cast<::Ogre::BorderPanelOverlayElement*>(element), values[0], values[1], exceptionCode);
						break;
					case 4:
						success = TryCallBorderPanelSetBorderSize4(reinterpret_cast<::Ogre::BorderPanelOverlayElement*>(element), values[0], values[1], values[2], values[3], exceptionCode);
						break;
					default:
						Logging::LogMessage("[EXU::Overlay] setParameter direct rejected elementName=%s element=%p kind=%d name=%s value=%s reason=invalid-border-size-arity", elementName.c_str(), element, static_cast<int>(kind), name.c_str(), value.c_str());
						return false;
					}
				}

				if (!success)
				{
					Logging::LogMessage("[EXU::Overlay] setParameter direct crashed elementName=%s element=%p name=%s value=%s code=0x%08X", elementName.c_str(), element, name.c_str(), value.c_str(), exceptionCode);
					return false;
				}

				Logging::LogMessage("[EXU::Overlay] setParameter direct elementName=%s element=%p name=%s value=%s success=1", elementName.c_str(), element, name.c_str(), value.c_str());
				return true;
			}

			return false;
		}

		bool SetOverlayParameter(const std::string& elementName, ::Ogre::OverlayElement* element, const std::string& name, const std::string& value)
		{
			using SetParameterFn = bool(__thiscall*)(void*, const std::string&, const std::string&);
			static SetParameterFn setParameter = []()
			{
				HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
				if (ogreMain == nullptr)
				{
					return static_cast<SetParameterFn>(nullptr);
				}

				return reinterpret_cast<SetParameterFn>(
					GetProcAddress(ogreMain, "?setParameter@StringInterface@Ogre@@UAE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"));
			}();

			if (element == nullptr)
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter unavailable element=%p name=%s value=%s setParameterAvailable=%d",
					element,
					name.c_str(),
					value.c_str(),
					setParameter != nullptr ? 1 : 0);
				return false;
			}

			if (name == "font_name")
			{
				EnsureOverlayRuntimeFont();
			}

			bool directHandled = false;
			const bool directSuccess = TrySetOverlayParameterDirect(elementName, element, name, value, directHandled);
			if (directHandled)
			{
				return directSuccess;
			}

			if (setParameter == nullptr)
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter unavailable element=%p name=%s value=%s setParameterAvailable=0",
					element,
					name.c_str(),
					value.c_str());
				return false;
			}

			bool success = false;
			unsigned int exceptionCode = 0;
			if (!TryCallSetOverlayParameter(setParameter, element, name, value, success, exceptionCode))
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter crashed element=%p name=%s value=%s code=0x%08X",
					element,
					name.c_str(),
					value.c_str(),
					exceptionCode);
				return false;
			}

			Logging::LogMessage(
				"[EXU::Overlay] setParameter element=%p name=%s value=%s success=%d",
				element,
				name.c_str(),
				value.c_str(),
				success ? 1 : 0);
			return success;
		}

		bool TryGetOverlayParameterValue(lua_State* L, int index, std::string& outValue)
		{
			const int type = lua_type(L, index);
			switch (type)
			{
			case LUA_TSTRING:
			{
				size_t length = 0;
				const char* value = lua_tolstring(L, index, &length);
				outValue.assign(value != nullptr ? value : "", length);
				return true;
			}
			case LUA_TNUMBER:
			{
				const lua_Number value = lua_tonumber(L, index);
				if (!std::isfinite(static_cast<double>(value)))
				{
					return false;
				}

				outValue = std::to_string(static_cast<double>(value));
				return true;
			}
			case LUA_TBOOLEAN:
				outValue = lua_toboolean(L, index) ? "true" : "false";
				return true;
			default:
				return false;
			}
		}

		void ResetOverlayRuntimeCaches() noexcept
		{
			overlayRuntimeResourcesReady = false;
			overlayRuntimeResourcesAttempted = false;
			overlayRuntimeFontReady = false;
			overlayRuntimeFontAttempted = false;
			overlayRuntimeFontScriptPath.clear();
		}

		bool TryDestroyOverlayByName(::Ogre::OverlayManager* manager, const std::string& name, bool& outDestroyed)
		{
			outDestroyed = false;
			if (manager == nullptr)
			{
				return false;
			}

			__try
			{
				if (manager->getByName(name) == nullptr)
				{
					return true;
				}

				manager->destroy(name);
				outDestroyed = true;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] destroy overlay crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroyOverlayElementByName(::Ogre::OverlayManager* manager, const std::string& name, bool& outDestroyed)
		{
			outDestroyed = false;
			if (manager == nullptr)
			{
				return false;
			}

			__try
			{
				if (!manager->hasOverlayElement(name, false))
				{
					return true;
				}

				manager->destroyOverlayElement(name, false);
				outDestroyed = true;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] destroy overlay element crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool ResetOverlaySupportInternal(const char* reason)
		{
			const char* resetReason = reason != nullptr ? reason : "unspecified";
			Logging::LogMessage(
				"[EXU::Overlay] ResetOverlaySupport begin reason=%s trackedOverlays=%u trackedElements=%u attachedSceneManagers=%u overlaySystem=%p",
				resetReason,
				static_cast<unsigned>(overlayVisibilityStates.size()),
				static_cast<unsigned>(knownElements.size()),
				static_cast<unsigned>(attachedOverlaySceneManagers.size()),
				overlaySystemInstance);

			bool success = true;
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			size_t destroyedOverlays = 0;
			size_t destroyedElements = 0;

			if (manager != nullptr)
			{
				std::vector<std::string> overlayNames;
				overlayNames.reserve(overlayVisibilityStates.size());
				for (const auto& [name, _] : overlayVisibilityStates)
				{
					overlayNames.push_back(name);
				}

				for (const std::string& overlayName : overlayNames)
				{
					bool destroyed = false;
					if (!TryDestroyOverlayByName(manager, overlayName, destroyed))
					{
						success = false;
					}
					else if (destroyed)
					{
						++destroyedOverlays;
					}
				}

				std::vector<std::pair<std::string, ElementKind>> elementNames;
				elementNames.reserve(knownElements.size());
				for (const auto& entry : knownElements)
				{
					elementNames.push_back(entry);
				}

				std::stable_sort(
					elementNames.begin(),
					elementNames.end(),
					[](const auto& lhs, const auto& rhs)
					{
						const bool lhsContainer = IsContainerKind(lhs.second);
						const bool rhsContainer = IsContainerKind(rhs.second);
						return lhsContainer == rhsContainer ? lhs.first < rhs.first : (!lhsContainer && rhsContainer);
					});

				for (const auto& [elementName, _] : elementNames)
				{
					bool destroyed = false;
					if (!TryDestroyOverlayElementByName(manager, elementName, destroyed))
					{
						success = false;
					}
					else if (destroyed)
					{
						++destroyedElements;
					}
				}
			}

			DetachOverlaySystemFromTrackedSceneManagers(overlaySystemInstance);
			DestroyOverlaySystemInstance();
			overlayVisibilityStates.clear();
			knownElements.clear();
			overlaySuppressionActive = false;
			ResetOverlayRuntimeCaches();

			Logging::LogMessage(
				"[EXU::Overlay] ResetOverlaySupport end reason=%s success=%d destroyedOverlays=%u destroyedElements=%u",
				resetReason,
				success ? 1 : 0,
				static_cast<unsigned>(destroyedOverlays),
				static_cast<unsigned>(destroyedElements));

			return success;
		}
	}

	void ShutdownOverlaySupport() noexcept
	{
		DestroyOverlayPauseHooks();
		DetachOverlaySystemFromTrackedSceneManagers(overlaySystemInstance);
		DestroyOverlaySystemInstance();
		ResetOverlayRuntimeCaches();
		knownElements.clear();
		overlayVisibilityStates.clear();
	}

	int ResetOverlaySupport(lua_State* L)
	{
		const char* reason = luaL_optstring(L, 1, "lua-reset");
		lua_pushboolean(L, ResetOverlaySupportInternal(reason) ? 1 : 0);
		return 1;
	}

	int CreateOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] CreateOverlay failed name=%s manager=null", name.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		if (manager->getByName(name) != nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		::Ogre::Overlay* overlay = manager->create(name);
		overlayVisibilityStates[name] = {};
		RefreshOverlaySuppressionState("create-overlay");
		Logging::LogMessage("[EXU::Overlay] CreateOverlay name=%s manager=%p overlay=%p", name.c_str(), manager, overlay);
		lua_pushboolean(L, overlay != nullptr);
		return 1;
	}

	int DestroyOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			return 0;
		}

		bool destroyed = false;
		TryDestroyOverlayByName(manager, name, destroyed);
		overlayVisibilityStates.erase(name);

		return 0;
	}

	int ShowOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlay missing name=%s", name.c_str());
			return 0;
		}

		OverlayVisibilityState& visibilityState = overlayVisibilityStates[name];
		visibilityState.requestedVisible = true;
		RefreshOverlaySuppressionState("show-overlay");

		void* viewport = GetCurrentViewportForOverlay();
		void* root = nullptr;
		TryGetRootSingleton(root);
		void* renderSystem = nullptr;
		TryGetRootRenderSystem(root, renderSystem);
		void* sharedListener = nullptr;
		const bool sharedListenerAvailable = TryGetRenderSystemSharedListener(sharedListener);
		void* renderSystemViewport = nullptr;
		const bool renderSystemViewportAvailable = TryGetRenderSystemViewport(renderSystem, renderSystemViewport);
		bool overlaysEnabledBefore = false;
		const bool overlaysEnabledBeforeAvailable = TryGetViewportOverlaysEnabled(viewport, overlaysEnabledBefore);

		Logging::LogMessage(
			"[EXU::Overlay] ShowOverlay pre name=%s overlay=%p viewport=%p root=%p renderSystem=%p sharedListener=%p sharedListenerAvailable=%d renderSystemViewport=%p renderSystemViewportAvailable=%d overlaysBefore=%s",
			name.c_str(),
			overlay,
			viewport,
			root,
			renderSystem,
			sharedListener,
			sharedListenerAvailable ? 1 : 0,
			renderSystemViewport,
			renderSystemViewportAvailable ? 1 : 0,
			DescribeOptionalBool(overlaysEnabledBeforeAvailable, overlaysEnabledBefore));

		SyncOverlayVisibilityState(name, visibilityState, "show-overlay");
		Logging::LogMessage(
			"[EXU::Overlay] ShowOverlay done name=%s overlay=%p viewport=%p requested=%d effective=%d suppressed=%d",
			name.c_str(),
			overlay,
			viewport,
			visibilityState.requestedVisible ? 1 : 0,
			visibilityState.effectiveVisible ? 1 : 0,
			overlaySuppressionActive ? 1 : 0);
		return 0;
	}

	int HideOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		OverlayVisibilityState& visibilityState = overlayVisibilityStates[name];
		visibilityState.requestedVisible = false;
		SyncOverlayVisibilityState(name, visibilityState, "hide-overlay");
		Logging::LogMessage(
			"[EXU::Overlay] HideOverlay name=%s overlay=%p requested=%d effective=%d",
			name.c_str(),
			overlay,
			visibilityState.requestedVisible ? 1 : 0,
			visibilityState.effectiveVisible ? 1 : 0);
		return 0;
	}

	int SetOverlayZOrder(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const int zOrder = static_cast<int>(luaL_checkinteger(L, 2));
		if (zOrder < 0 || zOrder > 650)
		{
			return luaL_argerror(L, 2, "z-order must be between 0 and 650");
		}

		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		overlay->setZOrder(static_cast<unsigned short>(zOrder));
		return 0;
	}

	int SetOverlayScroll(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float x = static_cast<float>(luaL_checknumber(L, 2));
		const float y = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(x))
		{
			return luaL_argerror(L, 2, "scroll x must be finite");
		}
		if (!std::isfinite(y))
		{
			return luaL_argerror(L, 3, "scroll y must be finite");
		}

		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		overlay->setScroll(x, y);
		return 0;
	}

	int CreateOverlayElement(lua_State* L)
	{
		const std::string typeName = luaL_checkstring(L, 1);
		const std::string instanceName = luaL_checkstring(L, 2);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] CreateOverlayElement failed type=%s name=%s manager=null", typeName.c_str(), instanceName.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		if (manager->hasOverlayElement(instanceName, false))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		::Ogre::OverlayElement* element = manager->createOverlayElement(typeName, instanceName, false);
		if (element != nullptr)
		{
			knownElements[instanceName] = GetElementKindByTypeName(typeName);
		}

		Logging::LogMessage(
			"[EXU::Overlay] CreateOverlayElement type=%s name=%s manager=%p element=%p",
			typeName.c_str(),
			instanceName.c_str(),
			manager,
			element);
		lua_pushboolean(L, element != nullptr);
		return 1;
	}

	int DestroyOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			return 0;
		}

		bool destroyed = false;
		TryDestroyOverlayElementByName(manager, name, destroyed);
		knownElements.erase(name);

		return 0;
	}

	int HasOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		lua_pushboolean(L, manager->hasOverlayElement(name, false));
		return 1;
	}

	int AddOverlay2D(lua_State* L)
	{
		const std::string overlayName = luaL_checkstring(L, 1);
		const std::string containerName = luaL_checkstring(L, 2);

		::Ogre::Overlay* overlay = FindOverlay(overlayName);
		::Ogre::OverlayContainer* container = FindOverlayContainer(containerName);
		if (overlay == nullptr || container == nullptr)
		{
			Logging::LogMessage(
				"[EXU::Overlay] AddOverlay2D skipped overlay=%s overlayPtr=%p container=%s containerPtr=%p",
				overlayName.c_str(),
				overlay,
				containerName.c_str(),
				container);
			return 0;
		}

		overlay->add2D(container);
		Logging::LogMessage(
			"[EXU::Overlay] AddOverlay2D overlay=%s overlayPtr=%p container=%s containerPtr=%p",
			overlayName.c_str(),
			overlay,
			containerName.c_str(),
			container);
		return 0;
	}

	int RemoveOverlay2D(lua_State* L)
	{
		const std::string overlayName = luaL_checkstring(L, 1);
		const std::string containerName = luaL_checkstring(L, 2);

		::Ogre::Overlay* overlay = FindOverlay(overlayName);
		::Ogre::OverlayContainer* container = FindOverlayContainer(containerName);
		if (overlay == nullptr || container == nullptr)
		{
			return 0;
		}

		overlay->remove2D(container);
		return 0;
	}

	int AddOverlayElementChild(lua_State* L)
	{
		const std::string parentName = luaL_checkstring(L, 1);
		const std::string childName = luaL_checkstring(L, 2);

		::Ogre::OverlayContainer* parent = FindOverlayContainer(parentName);
		::Ogre::OverlayElement* child = FindOverlayElement(childName);
		if (parent == nullptr || child == nullptr)
		{
			return 0;
		}

		parent->::Ogre::OverlayContainer::addChild(child);
		return 0;
	}

	int RemoveOverlayElementChild(lua_State* L)
	{
		const std::string parentName = luaL_checkstring(L, 1);
		const std::string childName = luaL_checkstring(L, 2);

		::Ogre::OverlayContainer* parent = FindOverlayContainer(parentName);
		if (parent == nullptr)
		{
			return 0;
		}

		parent->::Ogre::OverlayContainer::removeChild(childName);
		return 0;
	}

	int ShowOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlayElement missing name=%s", name.c_str());
			return 0;
		}

		element->::Ogre::OverlayElement::show();
		Logging::LogMessage("[EXU::Overlay] ShowOverlayElement name=%s element=%p", name.c_str(), element);
		return 0;
	}

	int HideOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->::Ogre::OverlayElement::hide();
		return 0;
	}

	int SetOverlayMetricsMode(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const int mode = static_cast<int>(luaL_checkinteger(L, 2));
		if (mode < static_cast<int>(::Ogre::GMM_RELATIVE) || mode > static_cast<int>(::Ogre::GMM_RELATIVE_ASPECT_ADJUSTED))
		{
			return luaL_argerror(L, 2, "metrics mode must be a valid exu.OVERLAY_METRICS value");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->::Ogre::OverlayElement::setMetricsMode(static_cast<::Ogre::GuiMetricsMode>(mode));
		return 0;
	}

	int SetOverlayPosition(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float left = static_cast<float>(luaL_checknumber(L, 2));
		const float top = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(left))
		{
			return luaL_argerror(L, 2, "left must be finite");
		}
		if (!std::isfinite(top))
		{
			return luaL_argerror(L, 3, "top must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->setPosition(left, top);
		return 0;
	}

	int SetOverlayDimensions(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float width = static_cast<float>(luaL_checknumber(L, 2));
		const float height = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(width))
		{
			return luaL_argerror(L, 2, "width must be finite");
		}
		if (!std::isfinite(height))
		{
			return luaL_argerror(L, 3, "height must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->setDimensions(width, height);
		return 0;
	}

	int SetOverlayMaterial(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string materialName = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial missing name=%s material=%s", name.c_str(), materialName.c_str());
			return 0;
		}

		element->::Ogre::OverlayElement::setMaterialName(materialName);
		Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial name=%s element=%p material=%s", name.c_str(), element, materialName.c_str());
		return 0;
	}

	int SetOverlayParameter(lua_State* L)
	{
		const std::string elementName = luaL_checkstring(L, 1);
		const std::string parameterName = luaL_checkstring(L, 2);
		std::string parameterValue;
		if (!TryGetOverlayParameterValue(L, 3, parameterValue))
		{
			return luaL_argerror(L, 3, "parameter value must be a finite number, boolean, or string");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(elementName);
		if (element == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = SetOverlayParameter(elementName, element, parameterName, parameterValue);
		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetOverlayColor(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const ExtraUtilities::Ogre::Color color = CheckColorOrSingles(L, 2);
		if (!IsFiniteColor(color))
		{
			return luaL_argerror(L, 2, "color components must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		const ::Ogre::ColourValue ogreColor{ color.r, color.g, color.b, color.a };
		element->::Ogre::OverlayElement::setColour(ogreColor);
		return 0;
	}

	int SetOverlayCaption(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string text = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (Native::TrySetTextAreaCaption(element, text.c_str()))
		{
			return 0;
		}

		SetOverlayParameter(name, element, "caption", text);
		return 0;
	}

	int SetOverlayTextFont(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string fontName = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		EnsureOverlayRuntimeFont();
		if (!overlayRuntimeFontReady)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextFont skipped name=%s font=%s runtimeFontReady=0", name.c_str(), fontName.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = Native::TrySetTextAreaFontName(element, fontName.c_str());
		if (!success)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextFont failed name=%s font=%s", name.c_str(), fontName.c_str());
		}
		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetOverlayTextColor(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const ExtraUtilities::Ogre::Color color = CheckColorOrSingles(L, 2);
		if (!IsFiniteColor(color))
		{
			return luaL_argerror(L, 2, "color components must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (!Native::TrySetTextAreaColor(element, color.r, color.g, color.b, color.a))
		{
			const std::string colorValue = std::to_string(color.r) + " "
				+ std::to_string(color.g) + " "
				+ std::to_string(color.b) + " "
				+ std::to_string(color.a);
			SetOverlayParameter(name, element, "colour_top", colorValue);
			SetOverlayParameter(name, element, "colour_bottom", colorValue);
		}
		return 0;
	}

	int SetOverlayTextCharHeight(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float charHeight = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(charHeight))
		{
			return luaL_argerror(L, 2, "char height must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (!Native::TrySetTextAreaCharHeight(element, charHeight))
		{
			SetOverlayParameter(name, element, "char_height", std::to_string(charHeight));
		}
		return 0;
	}
}
