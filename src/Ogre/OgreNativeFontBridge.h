#pragma once

namespace ExtraUtilities
{
	namespace Lua
	{
		namespace Overlay
		{
			namespace Native
			{
				bool TryAddResourceLocation(const char* location, const char* groupName) noexcept;
				bool TryParseFontScript(const char* scriptName, const char* groupName) noexcept;
				bool TryHasFontResource(const char* fontName, const char* groupName) noexcept;
				bool TryEnsureTrueTypeFont(
					const char* fontName,
					const char* groupName,
					const char* sourceName,
					float pointSize,
					unsigned int resolution,
					unsigned int firstCodePoint,
					unsigned int lastCodePoint) noexcept;
				bool TryEnsureImageFontFromSpriteTable(
					const char* fontName,
					const char* groupName,
					const char* textureName,
					const char* spriteTablePath) noexcept;
				bool TrySetTextAreaFontName(void* overlayElement, const char* fontName) noexcept;
				bool TrySetTextAreaCaption(void* overlayElement, const char* text) noexcept;
				bool TrySetTextAreaCharHeight(void* overlayElement, float charHeight) noexcept;
				bool TrySetTextAreaColor(void* overlayElement, float r, float g, float b, float a) noexcept;
			}
		}
	}
}
