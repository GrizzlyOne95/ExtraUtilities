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

#include "OgreOverlayElementOps.h"
#include "OgreOverlayRuntime.h"

#include "Ogre/Ogre.h"
#include "Util/Logging.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		// OgreOverlayShim.h cannot call Ogre's virtuals (its vtables are not
		// Ogre's), so element setters are called by qualified name. Calling the
		// OverlayElement version skipped the derived overrides: TextArea's
		// setMetricsMode/setMaterialName/setColour (pixel char height and space
		// width, top/bottom colours), BorderPanel's setMetricsMode (pixel
		// border sizes) and Panel's setMaterialName. These are the exported
		// overrides, plus the exported vftables used to identify the concrete
		// class of any element, including ones EXU did not create.
		struct ElementOverrides
		{
			using SetMetricsModeFn = void(__thiscall*)(void*, ::Ogre::GuiMetricsMode);
			using SetMaterialNameFn = void(__thiscall*)(void*, const ::Ogre::String&);
			using SetColourFn = void(__thiscall*)(void*, const ::Ogre::ColourValue&);

			// Each element's first vptr is its StringInterface vftable, the
			// first base of OverlayElement.
			const void* textAreaVftable = nullptr;
			const void* panelVftable = nullptr;
			const void* borderPanelVftable = nullptr;

			SetMetricsModeFn textAreaSetMetricsMode = nullptr;
			SetMetricsModeFn borderPanelSetMetricsMode = nullptr;
			SetMaterialNameFn textAreaSetMaterialName = nullptr;
			SetMaterialNameFn panelSetMaterialName = nullptr;
			SetColourFn textAreaSetColour = nullptr;
		};

		ElementOverrides ResolveElementOverrides()
		{
			ElementOverrides overrides;
			HMODULE ogreOverlay = GetOgreOverlayModule();
			if (ogreOverlay == nullptr)
			{
				return overrides;
			}

			const auto resolve = [ogreOverlay](const char* name)
			{
				return reinterpret_cast<const void*>(GetProcAddress(ogreOverlay, name));
			};

			overrides.textAreaVftable = resolve("??_7TextAreaOverlayElement@Ogre@@6BStringInterface@1@@");
			overrides.panelVftable = resolve("??_7PanelOverlayElement@Ogre@@6BStringInterface@1@@");
			overrides.borderPanelVftable = resolve("??_7BorderPanelOverlayElement@Ogre@@6BStringInterface@1@@");
			overrides.textAreaSetMetricsMode = reinterpret_cast<ElementOverrides::SetMetricsModeFn>(
				resolve("?setMetricsMode@TextAreaOverlayElement@Ogre@@UAEXW4GuiMetricsMode@2@@Z"));
			overrides.borderPanelSetMetricsMode = reinterpret_cast<ElementOverrides::SetMetricsModeFn>(
				resolve("?setMetricsMode@BorderPanelOverlayElement@Ogre@@UAEXW4GuiMetricsMode@2@@Z"));
			overrides.textAreaSetMaterialName = reinterpret_cast<ElementOverrides::SetMaterialNameFn>(
				resolve("?setMaterialName@TextAreaOverlayElement@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"));
			overrides.panelSetMaterialName = reinterpret_cast<ElementOverrides::SetMaterialNameFn>(
				resolve("?setMaterialName@PanelOverlayElement@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"));
			overrides.textAreaSetColour = reinterpret_cast<ElementOverrides::SetColourFn>(
				resolve("?setColour@TextAreaOverlayElement@Ogre@@UAEXABVColourValue@2@@Z"));
			return overrides;
		}

		const ElementOverrides& GetElementOverrides()
		{
			static const ElementOverrides overrides = ResolveElementOverrides();
			return overrides;
		}

		const void* ReadElementVftable(const ::Ogre::OverlayElement* element) noexcept
		{
			__try
			{
				return *reinterpret_cast<const void* const*>(element);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		// Concrete class by vftable identity. Anything else (a plain container
		// or a type from another plugin) uses the OverlayElement versions.
		ElementKind IdentifyElementKind(const ::Ogre::OverlayElement* element)
		{
			const auto& overrides = GetElementOverrides();
			const void* vftable = element != nullptr ? ReadElementVftable(element) : nullptr;
			if (vftable == nullptr)
			{
				return ElementKind::Unknown;
			}
			if (vftable == overrides.textAreaVftable)
			{
				return ElementKind::TextArea;
			}
			if (vftable == overrides.borderPanelVftable)
			{
				return ElementKind::BorderPanel;
			}
			if (vftable == overrides.panelVftable)
			{
				return ElementKind::Panel;
			}
			return ElementKind::Unknown;
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
	}

	namespace Detail
	{
		std::unordered_map<std::string, ElementKind> knownElements;

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

		void SetElementMetricsMode(::Ogre::OverlayElement* element, ::Ogre::GuiMetricsMode mode)
		{
			const auto& overrides = GetElementOverrides();
			switch (IdentifyElementKind(element))
			{
			case ElementKind::TextArea:
				if (overrides.textAreaSetMetricsMode != nullptr)
				{
					overrides.textAreaSetMetricsMode(element, mode);
					return;
				}
				break;
			case ElementKind::BorderPanel:
				if (overrides.borderPanelSetMetricsMode != nullptr)
				{
					overrides.borderPanelSetMetricsMode(element, mode);
					return;
				}
				break;
			default:
				break;
			}
			element->::Ogre::OverlayElement::setMetricsMode(mode);
		}

		// May throw (Ogre throws for a missing material).
		void SetElementMaterialName(::Ogre::OverlayElement* element, const ::Ogre::String& materialName)
		{
			const auto& overrides = GetElementOverrides();
			switch (IdentifyElementKind(element))
			{
			case ElementKind::TextArea:
				if (overrides.textAreaSetMaterialName != nullptr)
				{
					overrides.textAreaSetMaterialName(element, materialName);
					return;
				}
				break;
			case ElementKind::Panel:
			case ElementKind::BorderPanel:
				// BorderPanel inherits Panel's override.
				if (overrides.panelSetMaterialName != nullptr)
				{
					overrides.panelSetMaterialName(element, materialName);
					return;
				}
				break;
			default:
				break;
			}
			element->::Ogre::OverlayElement::setMaterialName(materialName);
		}

		void SetElementColour(::Ogre::OverlayElement* element, const ::Ogre::ColourValue& colour)
		{
			const auto& overrides = GetElementOverrides();
			if (IdentifyElementKind(element) == ElementKind::TextArea && overrides.textAreaSetColour != nullptr)
			{
				overrides.textAreaSetColour(element, colour);
				return;
			}
			element->::Ogre::OverlayElement::setColour(colour);
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

		ElementKind GetKnownElementKind(const std::string& elementName)
		{
			const auto it = knownElements.find(elementName);
			return it != knownElements.end() ? it->second : ElementKind::Unknown;
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
	}
}
