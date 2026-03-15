#include "OgreNativeFontBridge.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string>

#ifndef register
#define EXU_OGRE_RESTORE_REGISTER
#define register
#endif

#ifndef _STLP_MSVC
#define _STLP_MSVC 1
#endif

#include <Windows.h>

#include <OgreException.h>
#include <OgreFont.h>
#include <OgreFontManager.h>
#include <OgreOverlayElement.h>
#include <OgreResourceGroupManager.h>
#include <OgreTextAreaOverlayElement.h>

#ifdef EXU_OGRE_RESTORE_REGISTER
#undef register
#undef EXU_OGRE_RESTORE_REGISTER
#endif

#include <exception>

namespace
{
	void LogNativeOverlayMessage(const char* format, ...)
	{
		char buffer[1024]{};
		va_list args;
		va_start(args, format);
		vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
		va_end(args);

		OutputDebugStringA(buffer);
		OutputDebugStringA("\n");

		FILE* log = nullptr;
		if (fopen_s(&log, "exu.log", "a") == 0 && log != nullptr)
		{
			SYSTEMTIME localTime{};
			GetLocalTime(&localTime);
			std::fprintf(
				log,
				"[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
				localTime.wYear,
				localTime.wMonth,
				localTime.wDay,
				localTime.wHour,
				localTime.wMinute,
				localTime.wSecond,
				buffer);
			std::fclose(log);
		}
	}

	constexpr const char* kOverlayResourceLocationType = "FileSystem";

	struct FontPtrPod
	{
		Ogre::Font* pRep;
		void* pInfo;
	};

	HMODULE GetOgreMainModule()
	{
		static HMODULE module = GetModuleHandleA("OgreMain.dll");
		return module;
	}

	HMODULE GetOgreOverlayModule()
	{
		static HMODULE module = GetModuleHandleA("OgreOverlay.dll");
		return module;
	}

	template <typename T>
	T ResolveOgreProc(HMODULE module, const char* symbolName)
	{
		if (module == nullptr || symbolName == nullptr)
		{
			return static_cast<T>(nullptr);
		}

		return reinterpret_cast<T>(GetProcAddress(module, symbolName));
	}

	using CreateFontFn = FontPtrPod(__thiscall*)(Ogre::FontManager*, const Ogre::String&, const Ogre::String&, bool, Ogre::ManualResourceLoader*, const Ogre::NameValuePairList*);
	using InitialiseResourceGroupFn = void(__thiscall*)(Ogre::ResourceGroupManager*, const Ogre::String&);
	using OpenResourceFn = Ogre::DataStreamPtr(__thiscall*)(Ogre::ResourceGroupManager*, const Ogre::String&, const Ogre::String&, bool, Ogre::Resource*);

	struct SpriteGlyph
	{
		unsigned int codePoint;
		unsigned int u;
		unsigned int v;
		unsigned int width;
		unsigned int height;
		unsigned int refWidth;
		unsigned int refHeight;
	};

	bool TryParseSpriteGlyphLine(const std::string& line, SpriteGlyph& outGlyph)
	{
		const std::size_t firstQuote = line.find('"');
		if (firstQuote == std::string::npos)
		{
			return false;
		}

		const std::size_t secondQuote = line.find('"', firstQuote + 1);
		if (secondQuote == std::string::npos)
		{
			return false;
		}

		const std::string name = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
		if (name.rfind("char.", 0) != 0)
		{
			return false;
		}

		unsigned int codePoint = 0;
		if (std::sscanf(name.c_str(), "char.%u", &codePoint) != 1)
		{
			return false;
		}

		const char* fields = line.c_str() + secondQuote + 1;
		char materialName[64]{};
		unsigned int u = 0;
		unsigned int v = 0;
		unsigned int width = 0;
		unsigned int height = 0;
		unsigned int refWidth = 0;
		unsigned int refHeight = 0;
		unsigned int flags = 0;
		const int parsed = std::sscanf(
			fields,
			"%63s %u %u %u %u %u %u 0x%X",
			materialName,
			&u,
			&v,
			&width,
			&height,
			&refWidth,
			&refHeight,
			&flags);
		if (parsed < 8 || refWidth == 0 || refHeight == 0 || height == 0)
		{
			return false;
		}

		outGlyph.codePoint = codePoint;
		outGlyph.u = u;
		outGlyph.v = v;
		outGlyph.width = width;
		outGlyph.height = height;
		outGlyph.refWidth = refWidth;
		outGlyph.refHeight = refHeight;
		return true;
	}

	Ogre::ResourceGroupManager* GetResourceGroupManager()
	{
		return Ogre::ResourceGroupManager::getSingletonPtr();
	}

	Ogre::FontManager* GetFontManager()
	{
		return Ogre::FontManager::getSingletonPtr();
	}

	CreateFontFn ResolveCreateFontProc()
	{
		static const CreateFontFn fn = ResolveOgreProc<CreateFontFn>(
			GetOgreOverlayModule(),
			"?create@FontManager@Ogre@@QAE?AV?$SharedPtr@VFont@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0_NPAVManualResourceLoader@2@PBV?$map@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V12@U?$less@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@2@V?$STLAllocator@U?$pair@$$CBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V12@@std@@V?$CategorisedAllocPolicy@$0A@@Ogre@@@Ogre@@@5@@Z");
		return fn;
	}

	InitialiseResourceGroupFn ResolveInitialiseResourceGroupProc()
	{
		static const InitialiseResourceGroupFn fn = ResolveOgreProc<InitialiseResourceGroupFn>(
			GetOgreMainModule(),
			"?initialiseResourceGroup@ResourceGroupManager@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
		return fn;
	}

	OpenResourceFn ResolveOpenResourceProc()
	{
		static const OpenResourceFn fn = ResolveOgreProc<OpenResourceFn>(
			GetOgreMainModule(),
			"?openResource@ResourceGroupManager@Ogre@@QAE?AV?$SharedPtr@VDataStream@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0_NPAVResource@2@@Z");
		return fn;
	}

	bool EnsureResourceGroupExists(Ogre::ResourceGroupManager& manager, const char* groupName)
	{
		if (!manager.resourceGroupExists(groupName))
		{
			manager.createResourceGroup(groupName);
			LogNativeOverlayMessage("[EXU::Overlay] native createResourceGroup group=%s", groupName);
		}

		return true;
	}

	bool ParseFontScriptCpp(const char* scriptName, const char* groupName)
	{
		Ogre::ResourceGroupManager* resourceGroups = GetResourceGroupManager();
		const InitialiseResourceGroupFn initialiseResourceGroup = ResolveInitialiseResourceGroupProc();
		if (resourceGroups == nullptr || initialiseResourceGroup == nullptr || scriptName == nullptr || scriptName[0] == '\0')
		{
			return false;
		}

		initialiseResourceGroup(resourceGroups, groupName);
		return true;
	}

	bool TryParseFontScriptSeh(const char* scriptName, const char* groupName, unsigned int& outExceptionCode)
	{
		outExceptionCode = 0;

		__try
		{
			return ParseFontScriptCpp(scriptName, groupName);
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool HasFontResourceCpp(const char* fontName, const char* groupName)
	{
		Ogre::FontManager* manager = GetFontManager();
		if (manager == nullptr || fontName == nullptr)
		{
			return false;
		}

		const Ogre::String lookupGroup = (groupName != nullptr && groupName[0] != '\0') ? Ogre::String(groupName) : Ogre::String("");
		Ogre::FontPtr fontPtr = manager->getByName(fontName, lookupGroup);
		return fontPtr.getPointer() != nullptr;
	}

	bool TryHasFontResourceSeh(const char* fontName, const char* groupName, bool& outHasFont, unsigned int& outExceptionCode)
	{
		outHasFont = false;
		outExceptionCode = 0;

		__try
		{
			outHasFont = HasFontResourceCpp(fontName, groupName);
			return true;
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	Ogre::Font* GetOrCreateFontCpp(Ogre::FontManager* manager, const char* fontName, const char* groupName)
	{
		if (manager == nullptr || fontName == nullptr || groupName == nullptr)
		{
			return nullptr;
		}

		Ogre::FontPtr fontPtr = manager->getByName(fontName, groupName);
		Ogre::Font* font = fontPtr.getPointer();
		if (font != nullptr)
		{
			return font;
		}

		const CreateFontFn createFont = ResolveCreateFontProc();
		if (createFont == nullptr)
		{
			return nullptr;
		}

		const FontPtrPod createdFont = createFont(manager, fontName, groupName, false, nullptr, nullptr);
		return createdFont.pRep;
	}

	bool TryGetOrCreateFont(Ogre::FontManager* manager, const char* fontName, const char* groupName, Ogre::Font*& outFont,
		unsigned int& outExceptionCode)
	{
		outFont = nullptr;
		outExceptionCode = 0;

		__try
		{
			outFont = GetOrCreateFontCpp(manager, fontName, groupName);
			return true;
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void QueryTextureVisibilityCpp(
		Ogre::ResourceGroupManager* resourceGroups,
		const char* groupName,
		const char* textureName,
		bool& outVisibleInGroup,
		bool& outVisibleAnywhere)
	{
		outVisibleInGroup = resourceGroups->resourceExists(groupName, textureName);
		outVisibleAnywhere = resourceGroups->resourceExistsInAnyGroup(textureName);
	}

	bool TryQueryTextureVisibility(
		Ogre::ResourceGroupManager* resourceGroups,
		const char* groupName,
		const char* textureName,
		bool& outVisibleInGroup,
		bool& outVisibleAnywhere,
		unsigned int& outExceptionCode)
	{
		outVisibleInGroup = false;
		outVisibleAnywhere = false;
		outExceptionCode = 0;

		__try
		{
			QueryTextureVisibilityCpp(resourceGroups, groupName, textureName, outVisibleInGroup, outVisibleAnywhere);
			return true;
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool ConfigureTrueTypeFontCpp(
		Ogre::Font* font,
		const char* sourceName,
		float pointSize,
		unsigned int resolution,
		unsigned int firstCodePoint,
		unsigned int lastCodePoint)
	{
		if (font == nullptr || sourceName == nullptr)
		{
			return false;
		}

		font->setType(Ogre::FT_TRUETYPE);
		font->setSource(sourceName);
		font->setTrueTypeSize(pointSize);
		font->setTrueTypeResolution(resolution);
		font->clearCodePointRanges();
		font->addCodePointRange(Ogre::Font::CodePointRange(firstCodePoint, lastCodePoint));
		font->load();
		return true;
	}

	bool TryConfigureTrueTypeFont(
		Ogre::Font* font,
		const char* sourceName,
		float pointSize,
		unsigned int resolution,
		unsigned int firstCodePoint,
		unsigned int lastCodePoint,
		unsigned int& outExceptionCode)
	{
		outExceptionCode = 0;

		__try
		{
			return ConfigureTrueTypeFontCpp(font, sourceName, pointSize, resolution, firstCodePoint, lastCodePoint);
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool ConfigureImageFontCpp(Ogre::Font* font, const char* textureName, std::ifstream& spriteTable, unsigned int& outGlyphCount)
	{
		outGlyphCount = 0;
		if (font == nullptr || textureName == nullptr)
		{
			return false;
		}

		font->setType(Ogre::FT_IMAGE);
		font->setSource(textureName);
		font->setCharacterSpacer(0u);

		std::string line;
		while (std::getline(spriteTable, line))
		{
			SpriteGlyph glyph{};
			if (!TryParseSpriteGlyphLine(line, glyph))
			{
				continue;
			}

			const Ogre::Real textureAspect = static_cast<Ogre::Real>(glyph.refWidth) / static_cast<Ogre::Real>(glyph.refHeight);
			const Ogre::Real u1 = static_cast<Ogre::Real>(glyph.u) / static_cast<Ogre::Real>(glyph.refWidth);
			const Ogre::Real v1 = static_cast<Ogre::Real>(glyph.v) / static_cast<Ogre::Real>(glyph.refHeight);
			const Ogre::Real u2 = static_cast<Ogre::Real>(glyph.u + glyph.width) / static_cast<Ogre::Real>(glyph.refWidth);
			const Ogre::Real v2 = static_cast<Ogre::Real>(glyph.v + glyph.height) / static_cast<Ogre::Real>(glyph.refHeight);
			font->setGlyphTexCoords(glyph.codePoint, u1, v1, u2, v2, textureAspect);
			++outGlyphCount;
		}

		font->load();
		return outGlyphCount > 0;
	}

	bool TryConfigureImageFont(Ogre::Font* font, const char* textureName, std::ifstream& spriteTable, unsigned int& outGlyphCount,
		unsigned int& outExceptionCode)
	{
		outGlyphCount = 0;
		outExceptionCode = 0;

		__try
		{
			return ConfigureImageFontCpp(font, textureName, spriteTable, outGlyphCount);
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void SetTextAreaFontNameCpp(void* overlayElement, const char* fontName)
	{
		auto* textArea = static_cast<Ogre::TextAreaOverlayElement*>(overlayElement);
		textArea->setFontName(fontName);
	}

	bool TrySetTextAreaFontNameSeh(void* overlayElement, const char* fontName, unsigned int& outExceptionCode)
	{
		outExceptionCode = 0;

		__try
		{
			SetTextAreaFontNameCpp(overlayElement, fontName);
			return true;
		}
		__except (outExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

namespace ExtraUtilities
{
namespace Lua
{
namespace Overlay
{
namespace Native
{
	bool TryParseFontScript(const char* scriptName, const char* groupName) noexcept
	{
		if (scriptName == nullptr || scriptName[0] == '\0' || groupName == nullptr || groupName[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript rejected script=%s group=%s",
				scriptName != nullptr ? scriptName : "<null>",
				groupName != nullptr ? groupName : "<null>");
			return false;
		}

		try
		{
			unsigned int exceptionCode = 0;
			if (!TryParseFontScriptSeh(scriptName, groupName, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript seh script=%s group=%s code=0x%08X",
					scriptName,
					groupName,
					exceptionCode);
				return false;
			}

			LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript script=%s group=%s", scriptName, groupName);
			return true;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript ogre exception script=%s group=%s what=%s",
				scriptName,
				groupName,
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript threw script=%s group=%s what=%s",
				scriptName,
				groupName,
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native parseFontScript threw script=%s group=%s",
				scriptName,
				groupName);
			return false;
		}
	}

	bool TryHasFontResource(const char* fontName, const char* groupName) noexcept
	{
		if (fontName == nullptr || fontName[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native hasFont rejected font=%s group=%s",
				fontName != nullptr ? fontName : "<null>",
				groupName != nullptr ? groupName : "<null>");
			return false;
		}

		try
		{
			bool hasFont = false;
			unsigned int exceptionCode = 0;
			if (!TryHasFontResourceSeh(fontName, groupName, hasFont, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native hasFont seh font=%s group=%s code=0x%08X",
					fontName,
					groupName != nullptr ? groupName : "<autodetect>",
					exceptionCode);
				return false;
			}

			LogNativeOverlayMessage("[EXU::Overlay] native hasFont font=%s group=%s value=%d",
				fontName,
				groupName != nullptr ? groupName : "<autodetect>",
				hasFont ? 1 : 0);
			return hasFont;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native hasFont ogre exception font=%s group=%s what=%s",
				fontName,
				groupName != nullptr ? groupName : "<autodetect>",
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native hasFont threw font=%s group=%s what=%s",
				fontName,
				groupName != nullptr ? groupName : "<autodetect>",
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native hasFont threw font=%s group=%s",
				fontName,
				groupName != nullptr ? groupName : "<autodetect>");
			return false;
		}
	}

	bool TryAddResourceLocation(const char* location, const char* groupName) noexcept
	{
		if (location == nullptr || location[0] == '\0' || groupName == nullptr || groupName[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation rejected location=%s group=%s",
				location != nullptr ? location : "<null>",
				groupName != nullptr ? groupName : "<null>");
			return false;
		}

		try
		{
			Ogre::ResourceGroupManager* manager = GetResourceGroupManager();
			if (manager == nullptr)
			{
				LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation unavailable location=%s group=%s manager=null",
					location,
					groupName);
				return false;
			}

			EnsureResourceGroupExists(*manager, groupName);
			manager->addResourceLocation(location, kOverlayResourceLocationType, groupName, false, true);
			LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation location=%s group=%s", location, groupName);
			return true;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation ogre exception location=%s group=%s what=%s",
				location,
				groupName,
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation threw location=%s group=%s what=%s",
				location,
				groupName,
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native addResourceLocation threw location=%s group=%s",
				location,
				groupName);
			return false;
		}
	}

	bool TryEnsureTrueTypeFont(
		const char* fontName,
		const char* groupName,
		const char* sourceName,
		float pointSize,
		unsigned int resolution,
		unsigned int firstCodePoint,
		unsigned int lastCodePoint) noexcept
	{
		if (fontName == nullptr || fontName[0] == '\0'
			|| groupName == nullptr || groupName[0] == '\0'
			|| sourceName == nullptr || sourceName[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native font ensure rejected font=%s group=%s source=%s",
				fontName != nullptr ? fontName : "<null>",
				groupName != nullptr ? groupName : "<null>",
				sourceName != nullptr ? sourceName : "<null>");
			return false;
		}

		try
		{
			Ogre::FontManager* manager = GetFontManager();
			if (manager == nullptr)
			{
				LogNativeOverlayMessage("[EXU::Overlay] native font ensure unavailable font=%s group=%s manager=null",
					fontName,
					groupName);
				return false;
			}

			Ogre::Font* font = nullptr;
			unsigned int exceptionCode = 0;
			if (!TryGetOrCreateFont(manager, fontName, groupName, font, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native font ensure seh font=%s group=%s code=0x%08X",
					fontName,
					groupName,
					exceptionCode);
				return false;
			}

			if (font == nullptr)
			{
				LogNativeOverlayMessage("[EXU::Overlay] native font ensure unavailable font=%s group=%s createFont=0",
					fontName,
					groupName);
				return false;
			}

			if (!TryConfigureTrueTypeFont(font, sourceName, pointSize, resolution, firstCodePoint, lastCodePoint, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native font ensure seh font=%s group=%s source=%s code=0x%08X",
					fontName,
					groupName,
					sourceName,
					exceptionCode);
				return false;
			}

			LogNativeOverlayMessage("[EXU::Overlay] native font ready name=%s group=%s source=%s font=%p",
				fontName,
				groupName,
				sourceName,
				font);
			return true;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native font ensure ogre exception font=%s group=%s source=%s what=%s",
				fontName,
				groupName,
				sourceName,
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native font ensure threw font=%s group=%s source=%s what=%s",
				fontName,
				groupName,
				sourceName,
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native font ensure threw font=%s group=%s source=%s",
				fontName,
				groupName,
				sourceName);
			return false;
		}
	}

	bool TryEnsureImageFontFromSpriteTable(
		const char* fontName,
		const char* groupName,
		const char* textureName,
		const char* spriteTablePath) noexcept
	{
		if (fontName == nullptr || fontName[0] == '\0'
			|| groupName == nullptr || groupName[0] == '\0'
			|| textureName == nullptr || textureName[0] == '\0'
			|| spriteTablePath == nullptr || spriteTablePath[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native image font rejected font=%s group=%s texture=%s table=%s",
				fontName != nullptr ? fontName : "<null>",
				groupName != nullptr ? groupName : "<null>",
				textureName != nullptr ? textureName : "<null>",
				spriteTablePath != nullptr ? spriteTablePath : "<null>");
			return false;
		}

		try
		{
			std::ifstream spriteTable(spriteTablePath);
			if (!spriteTable.is_open())
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font failed to open table=%s", spriteTablePath);
				return false;
			}

			Ogre::ResourceGroupManager* resourceGroups = GetResourceGroupManager();
			Ogre::FontManager* manager = GetFontManager();
			if (resourceGroups == nullptr || manager == nullptr)
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font unavailable font=%s group=%s resourceGroups=%d manager=%d",
					fontName,
					groupName,
					resourceGroups != nullptr ? 1 : 0,
					manager != nullptr ? 1 : 0);
				return false;
			}

			bool textureVisibleInGroup = false;
			bool textureVisibleAnywhere = false;
			unsigned int exceptionCode = 0;
			if (!TryQueryTextureVisibility(
				resourceGroups,
				groupName,
				textureName,
				textureVisibleInGroup,
				textureVisibleAnywhere,
				exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font texture visibility seh font=%s group=%s texture=%s code=0x%08X",
					fontName,
					groupName,
					textureName,
					exceptionCode);
				return false;
			}

			LogNativeOverlayMessage("[EXU::Overlay] native image font texture visibility font=%s group=%s texture=%s inGroup=%d inAny=%d",
				fontName,
				groupName,
				textureName,
				textureVisibleInGroup ? 1 : 0,
				textureVisibleAnywhere ? 1 : 0);

			Ogre::Font* font = nullptr;
			if (!TryGetOrCreateFont(manager, fontName, groupName, font, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font seh font=%s group=%s create/load code=0x%08X",
					fontName,
					groupName,
					exceptionCode);
				return false;
			}

			if (font == nullptr)
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font unavailable font=%s group=%s createFont=0",
					fontName,
					groupName);
				return false;
			}

			unsigned int glyphCount = 0;
			if (!TryConfigureImageFont(font, textureName, spriteTable, glyphCount, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native image font seh font=%s group=%s texture=%s table=%s code=0x%08X",
					fontName,
					groupName,
					textureName,
					spriteTablePath,
					exceptionCode);
				return false;
			}
			LogNativeOverlayMessage("[EXU::Overlay] native image font ready name=%s group=%s texture=%s table=%s glyphs=%u font=%p",
				fontName,
				groupName,
				textureName,
				spriteTablePath,
				glyphCount,
				font);
			return glyphCount > 0;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native image font ogre exception font=%s group=%s texture=%s table=%s what=%s",
				fontName,
				groupName,
				textureName,
				spriteTablePath,
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native image font threw font=%s group=%s texture=%s table=%s what=%s",
				fontName,
				groupName,
				textureName,
				spriteTablePath,
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native image font threw font=%s group=%s texture=%s table=%s",
				fontName,
				groupName,
				textureName,
				spriteTablePath);
			return false;
		}
	}

	bool TrySetTextAreaFontName(void* overlayElement, const char* fontName) noexcept
	{
		if (overlayElement == nullptr || fontName == nullptr || fontName[0] == '\0')
		{
			LogNativeOverlayMessage("[EXU::Overlay] native setFontName rejected element=%p font=%s",
				overlayElement,
				fontName != nullptr ? fontName : "<null>");
			return false;
		}

		try
		{
			unsigned int exceptionCode = 0;
			if (!TrySetTextAreaFontNameSeh(overlayElement, fontName, exceptionCode))
			{
				LogNativeOverlayMessage("[EXU::Overlay] native setFontName seh element=%p font=%s code=0x%08X",
					overlayElement,
					fontName,
					exceptionCode);
				return false;
			}
			LogNativeOverlayMessage("[EXU::Overlay] native setFontName element=%p font=%s", overlayElement, fontName);
			return true;
		}
		catch (const Ogre::Exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native setFontName ogre exception element=%p font=%s what=%s",
				overlayElement,
				fontName,
				ex.getFullDescription().c_str());
			return false;
		}
		catch (const std::exception& ex)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native setFontName threw element=%p font=%s what=%s",
				overlayElement,
				fontName,
				ex.what());
			return false;
		}
		catch (...)
		{
			LogNativeOverlayMessage("[EXU::Overlay] native setFontName threw element=%p font=%s",
				overlayElement,
				fontName);
			return false;
		}
	}
}
}
}
}
