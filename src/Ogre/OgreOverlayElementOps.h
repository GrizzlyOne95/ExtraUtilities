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

#include "Ogre/OgreOverlayShim.h"

#include <string>
#include <unordered_map>
#include <vector>

// Typed OverlayElement operations: element-kind identification by vftable,
// the exported Panel/BorderPanel/TextArea setters called under SEH, and the
// kind-checked parameter setters behind exu.SetOverlayParameter.

namespace ExtraUtilities::Lua::Overlay
{
	namespace Detail
	{
		enum class ElementKind
		{
			Unknown,
			Panel,
			BorderPanel,
			TextArea,
		};
		extern std::unordered_map<std::string, ElementKind> knownElements;
		ElementKind GetElementKindByTypeName(const std::string& typeName);
		bool IsContainerKind(ElementKind kind);
		void SetElementMetricsMode(::Ogre::OverlayElement* element, ::Ogre::GuiMetricsMode mode);
		void SetElementMaterialName(::Ogre::OverlayElement* element, const ::Ogre::String& materialName);
		void SetElementColour(::Ogre::OverlayElement* element, const ::Ogre::ColourValue& colour);
		bool TryCallSetOverlayParameter(
			bool(__thiscall* setParameter)(void*, const std::string&, const std::string&),
			::Ogre::OverlayElement* element,
			const std::string& name,
			const std::string& value,
			bool& outSuccess,
			unsigned int& outExceptionCode);
		bool TryShowOverlay(::Ogre::Overlay* overlay, unsigned int& outExceptionCode);
		bool TryHideOverlay(::Ogre::Overlay* overlay, unsigned int& outExceptionCode);
		ElementKind GetKnownElementKind(const std::string& elementName);
		bool TrySetOverlayParameterDirect(
			const std::string& elementName,
			::Ogre::OverlayElement* element,
			const std::string& name,
			const std::string& value,
			bool& outHandled);
	}

	using namespace Detail;
}
