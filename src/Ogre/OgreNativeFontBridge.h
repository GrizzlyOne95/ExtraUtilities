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
				bool TryResetFontResourceGroupIfStale(const char* fontName, const char* groupName) noexcept;
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
