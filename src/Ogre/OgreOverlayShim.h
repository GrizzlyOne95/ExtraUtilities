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

#include <utility>
#include <string>

namespace Ogre
{
	using Real = float;
	using String = std::string;
	using uint32 = unsigned int;

	template <class T>
	class SharedPtr
	{
	public:
		T* pRep = nullptr;
		void* pInfo = nullptr;

		SharedPtr() = default;
		SharedPtr(const SharedPtr& other) : pRep(other.pRep), pInfo(other.pInfo) {}
		SharedPtr& operator=(const SharedPtr& other)
		{
			pRep = other.pRep;
			pInfo = other.pInfo;
			return *this;
		}
		~SharedPtr() {}

		T* getPointer() const { return pRep; }
		bool isNull() const { return pRep == nullptr; }
	};

	enum GuiMetricsMode
	{
		GMM_RELATIVE,
		GMM_PIXELS,
		GMM_RELATIVE_ASPECT_ADJUSTED
	};

	class ColourValue
	{
	public:
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;

		ColourValue() = default;
		ColourValue(float red, float green, float blue, float alpha = 1.0f)
			: r(red), g(green), b(blue), a(alpha) {}
	};

	class OverlayElement
	{
	public:
		__declspec(dllimport) virtual void show(void);
		__declspec(dllimport) virtual void hide(void);
		__declspec(dllimport) virtual void setColour(const ColourValue& col);
		__declspec(dllimport) virtual void setMetricsMode(GuiMetricsMode gmm);
		__declspec(dllimport) virtual void setMaterialName(const String& matName);
		__declspec(dllimport) void setDimensions(Real width, Real height);
		__declspec(dllimport) void setPosition(Real left, Real top);
	};

	class Resource
	{
	public:
		__declspec(dllimport) virtual void load(bool backgroundThread = false);
	};

	enum FontType
	{
		FT_TRUETYPE = 1,
		FT_IMAGE = 2
	};

	class Font : public Resource
	{
	public:
		using CodePointRange = std::pair<uint32, uint32>;

		__declspec(dllimport) void setType(FontType ftype);
		__declspec(dllimport) void setSource(const String& source);
		__declspec(dllimport) void setTrueTypeSize(Real ttfSize);
		__declspec(dllimport) void setTrueTypeResolution(uint32 ttfResolution);
		__declspec(dllimport) void clearCodePointRanges(void);
		__declspec(dllimport) void addCodePointRange(const CodePointRange& range);
	};

	class OverlayContainer
	{
	public:
		__declspec(dllimport) virtual void addChild(OverlayElement* elem);
		__declspec(dllimport) virtual void removeChild(const String& name);
	};

	class PanelOverlayElement : public OverlayElement
	{
	public:
		__declspec(dllimport) void setTiling(Real x, Real y, unsigned short layer = 0);
		__declspec(dllimport) void setUV(Real u1, Real v1, Real u2, Real v2);
		__declspec(dllimport) void setTransparent(bool isTransparent);
	};

	class BorderPanelOverlayElement : public PanelOverlayElement
	{
	public:
		__declspec(dllimport) void setBorderSize(Real size);
		__declspec(dllimport) void setBorderSize(Real sides, Real topAndBottom);
		__declspec(dllimport) void setBorderSize(Real left, Real right, Real top, Real bottom);
		__declspec(dllimport) void setBorderMaterialName(const String& name);
	};

	class TextAreaOverlayElement : public OverlayElement
	{
	public:
		enum Alignment
		{
			Left,
			Right,
			Center
		};

		__declspec(dllimport) void setSpaceWidth(Real width);
		__declspec(dllimport) void setColourTop(const ColourValue& col);
		__declspec(dllimport) void setColourBottom(const ColourValue& col);
		__declspec(dllimport) void setAlignment(Alignment a);
	};

	class Overlay
	{
	public:
		__declspec(dllimport) void show(void);
		__declspec(dllimport) void hide(void);
		__declspec(dllimport) void add2D(OverlayContainer* cont);
		__declspec(dllimport) void remove2D(OverlayContainer* cont);
		__declspec(dllimport) void setZOrder(unsigned short zorder);
		__declspec(dllimport) void setScroll(Real x, Real y);
	};

	class OverlayManager
	{
	public:
		__declspec(dllimport) static OverlayManager* getSingletonPtr(void);
		__declspec(dllimport) Overlay* create(const String& name);
		__declspec(dllimport) Overlay* getByName(const String& name);
		__declspec(dllimport) void destroy(const String& name);
		__declspec(dllimport) OverlayElement* createOverlayElement(const String& typeName, const String& instanceName, bool isTemplate = false);
		__declspec(dllimport) OverlayElement* getOverlayElement(const String& name, bool isTemplate = false);
		__declspec(dllimport) bool hasOverlayElement(const String& name, bool isTemplate = false);
		__declspec(dllimport) void destroyOverlayElement(const String& instanceName, bool isTemplate = false);
	};
}
