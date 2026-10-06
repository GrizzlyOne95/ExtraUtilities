/* Copyright (C) 2026 GrizzlyOne95
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

// Engine-free maths of the path-grid face footprint (exu.pathing). Shared
// with the host tests and mirrored line for line by
// tools/pathblock_dryrun.py; Docs/PATH_BLOCK.md states the contract that
// OpenShim implements too, so any change here is a contract change.

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ExtraUtilities::PathBlockMath
{
	// Bits BuildingBlock ORs into a cell; BuildingUnblock clears them.
	constexpr std::uint8_t kBuildingBits = 0x0B;
	// Terrain bits ProcessCliffs writes: 3 cliff, 2 steep, 1 slope.
	constexpr std::uint8_t kCliffBits = 0x03;
	constexpr std::uint8_t kSteepBit = 0x02;
	constexpr std::uint8_t kSlopeBit = 0x01;
	// PerimeterBlock's bit (class-5 objects, non-cliff cells only).
	constexpr std::uint8_t kPerimeterBit = 0x08;

	constexpr float kDefaultHeight1 = 1.5f;
	constexpr float kDefaultHeight2 = 3.0f;

	enum class Mode : std::uint8_t
	{
		Box,   // stock: the whole oriented bbox
		Faces, // only cells whose centre is inside solid geometry
		None,  // no cells
	};

	inline const char* ModeName(Mode mode) noexcept
	{
		switch (mode)
		{
		case Mode::Faces: return "faces";
		case Mode::None: return "none";
		default: return "box";
		}
	}

	// Trims blanks and one pair of surrounding quotes.
	inline void TrimValue(const char*& begin, const char*& end) noexcept
	{
		while (begin < end && std::isspace(static_cast<unsigned char>(*begin)))
			++begin;
		while (end > begin && std::isspace(static_cast<unsigned char>(end[-1])))
			--end;
		if (end - begin >= 2 && (*begin == '"' || *begin == '\'') && end[-1] == *begin)
		{
			++begin;
			--end;
		}
		while (begin < end && std::isspace(static_cast<unsigned char>(*begin)))
			++begin;
		while (end > begin && std::isspace(static_cast<unsigned char>(end[-1])))
			--end;
	}

	inline bool EqualsNoCase(const char* begin, const char* end, const char* word) noexcept
	{
		const std::size_t length = static_cast<std::size_t>(end - begin);
		if (std::strlen(word) != length)
			return false;
		for (std::size_t i = 0; i < length; ++i)
		{
			if (std::tolower(static_cast<unsigned char>(begin[i])) != word[i])
				return false;
		}
		return true;
	}

	// ODF pathBlock value: "box" | "faces" | "none"; null, empty and unknown
	// values are "box" (stock).
	inline Mode ParseMode(const char* text) noexcept
	{
		if (text == nullptr)
			return Mode::Box;
		const char* begin = text;
		const char* end = text + std::strlen(text);
		TrimValue(begin, end);
		if (EqualsNoCase(begin, end, "faces"))
			return Mode::Faces;
		if (EqualsNoCase(begin, end, "none"))
			return Mode::None;
		return Mode::Box;
	}

	// ODF pathBlockHeight/pathBlockHeight2: a finite number, else the default.
	inline float ParseHeight(const char* text, float fallback) noexcept
	{
		if (text == nullptr)
			return fallback;
		const char* begin = text;
		const char* end = text + std::strlen(text);
		TrimValue(begin, end);
		if (begin == end)
			return fallback;
		char buffer[64] = {};
		const std::size_t length = static_cast<std::size_t>(end - begin);
		if (length >= sizeof(buffer))
			return fallback;
		std::memcpy(buffer, begin, length);
		char* parsedEnd = nullptr;
		const double value = std::strtod(buffer, &parsedEnd);
		if (parsedEnd == buffer || !std::isfinite(value) || std::fabs(value) > 1.0e6)
			return fallback;
		return static_cast<float>(value);
	}

	struct Vec3f
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Triangle
	{
		Vec3f a;
		Vec3f b;
		Vec3f c;
	};

	// Rigid 3x4 transform, BZ row layout: world = x*right + y*up + z*front + posit.
	struct Mat
	{
		double right[3] = { 1.0, 0.0, 0.0 };
		double up[3] = { 0.0, 1.0, 0.0 };
		double front[3] = { 0.0, 0.0, 1.0 };
		double posit[3] = { 0.0, 0.0, 0.0 };
	};

	inline void Rotate(const Mat& m, const double v[3], double out[3]) noexcept
	{
		for (int i = 0; i < 3; ++i)
			out[i] = v[0] * m.right[i] + v[1] * m.up[i] + v[2] * m.front[i];
	}

	inline void Apply(const Mat& m, const double v[3], double out[3]) noexcept
	{
		Rotate(m, v, out);
		for (int i = 0; i < 3; ++i)
			out[i] += m.posit[i];
	}

	// a, then b (a child's local matrix, then its parent's accumulated one).
	inline Mat Then(const Mat& a, const Mat& b) noexcept
	{
		Mat out;
		Rotate(b, a.right, out.right);
		Rotate(b, a.up, out.up);
		Rotate(b, a.front, out.front);
		Apply(b, a.posit, out.posit);
		return out;
	}

	// Nonzero-winding count of a horizontal ray cast in local +X from (x, y, z)
	// through the triangles. A triangle counts +1 when its normal (b-a)x(c-a)
	// points against the ray (entering) and -1 when along it (exiting). From
	// inside an outward-wound closed part only its exit is ahead, so the count
	// is -1 there (+1 for an inward-wound part; the nonzero rule does not
	// care, and overlapping parts add up instead of cancelling). Each triangle is projected on the (y, z)
	// plane and tested with edge functions; points on a shared edge belong to
	// exactly one of the two triangles (top-left style ownership), so a ray
	// through an edge or vertex is never counted twice or missed. Triangles
	// edge-on to the ray (zero projected area) never count. A hit counts only
	// strictly ahead of the start (hit x > x).
	inline bool OwnsEdge(double du, double dv) noexcept
	{
		return dv > 0.0 || (dv == 0.0 && du < 0.0);
	}

	inline int WindingAt(const Triangle* tris, std::size_t count, double x, double y, double z) noexcept
	{
		int winding = 0;
		for (std::size_t i = 0; i < count; ++i)
		{
			const Triangle& t = tris[i];
			const double ay = t.a.y, az = t.a.z;
			const double by = t.b.y, bz = t.b.z;
			const double cy = t.c.y, cz = t.c.z;
			const double area = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
			if (area == 0.0)
				continue;
			// Counter-clockwise in (y, z): P, Q, R.
			const Vec3f& P = t.a;
			const Vec3f& Q = area > 0.0 ? t.b : t.c;
			const Vec3f& R = area > 0.0 ? t.c : t.b;
			const double py = P.y, pz = P.z, qy = Q.y, qz = Q.z, ry = R.y, rz = R.z;

			const double w0 = (ry - qy) * (z - qz) - (rz - qz) * (y - qy); // edge Q->R, weight of P
			if (w0 < 0.0 || (w0 == 0.0 && !OwnsEdge(ry - qy, rz - qz)))
				continue;
			const double w1 = (py - ry) * (z - rz) - (pz - rz) * (y - ry); // edge R->P, weight of Q
			if (w1 < 0.0 || (w1 == 0.0 && !OwnsEdge(py - ry, pz - rz)))
				continue;
			const double w2 = (qy - py) * (z - pz) - (qz - pz) * (y - py); // edge P->Q, weight of R
			if (w2 < 0.0 || (w2 == 0.0 && !OwnsEdge(qy - py, qz - pz)))
				continue;

			const double hitX = (w0 * P.x + w1 * Q.x + w2 * R.x) / std::fabs(area);
			if (hitX > x)
				winding += area < 0.0 ? 1 : -1;
		}
		return winding;
	}

	// A cell is solid when its centre is inside geometry at either sample height.
	inline bool SolidAt(const Triangle* tris, std::size_t count, double x, double z, double h1, double h2) noexcept
	{
		return WindingAt(tris, count, x, h1, z) != 0 || WindingAt(tris, count, x, h2, z) != 0;
	}

	// The object's ground placement, as BlockCells reads it from the root
	// matrix: world (x, z) = right.xz * lx + front.xz * lz + posit.xz. The up
	// row and y are ignored, exactly as in the stock rasterizer.
	struct Placement
	{
		float rightX = 1.0f;
		float rightZ = 0.0f;
		float frontX = 0.0f;
		float frontZ = 1.0f;
		double positX = 0.0;
		double positZ = 0.0;
	};

	// World (x, z) back to root-local (lx, lz). False for a degenerate matrix.
	inline bool WorldToLocal(const Placement& p, double wx, double wz, double& lx, double& lz) noexcept
	{
		const double rx = p.rightX, rz = p.rightZ, fx = p.frontX, fz = p.frontZ;
		const double det = rx * fz - fx * rz;
		if (!(std::fabs(det) > 1.0e-9))
			return false;
		const double dx = wx - p.positX;
		const double dz = wz - p.positZ;
		lx = (fz * dx - fx * dz) / det;
		lz = (-rz * dx + rx * dz) / det;
		return true;
	}

	// Strictly inside the bbox footprint (min/max are root-local x and z).
	inline bool InsideBox(double lx, double lz, const float bmin[3], const float bmax[3]) noexcept
	{
		return lx > bmin[0] && lx < bmax[0] && lz > bmin[2] && lz < bmax[2];
	}

	// The stock BlockCells corner vertices (float products, double posit, float
	// result), in its order: (min.x,min.z) (max.x,min.z) (max.x,max.z) (min.x,max.z).
	struct BoxPolygon
	{
		float x[4] = {};
		float z[4] = {};
	};

	inline BoxPolygon MakeBoxPolygon(const Placement& p, const float bmin[3], const float bmax[3]) noexcept
	{
		const float lx[4] = { bmin[0], bmax[0], bmax[0], bmin[0] };
		const float lz[4] = { bmin[2], bmin[2], bmax[2], bmax[2] };
		BoxPolygon poly;
		for (int i = 0; i < 4; ++i)
		{
			const float sx = p.rightX * lx[i] + p.frontX * lz[i];
			const float sz = p.rightZ * lx[i] + p.frontZ * lz[i];
			poly.x[i] = static_cast<float>(static_cast<double>(sx) + p.positX);
			poly.z[i] = static_cast<float>(static_cast<double>(sz) + p.positZ);
		}
		return poly;
	}

	// Clone of the stock BuildingBlock (GOG 0x004693C0) test for one cell
	// centre: blocked when the centre is inside the polygon grown by half a
	// cell, with rounded corners.
	inline bool BoxCellBlocked(const BoxPolygon& poly, float cx, float cz, float half) noexcept
	{
		constexpr int count = 4;
		int best = -1;
		float bestDistance = -3.4028235e+38f;
		float prevX = poly.x[count - 1];
		float prevZ = poly.z[count - 1];
		for (int i = 0; i < count; ++i)
		{
			float ex = poly.x[i] - prevX;
			float ez = poly.z[i] - prevZ;
			const float length = std::sqrt(ex * ex + ez * ez);
			if (length > 0.0f)
			{
				ex /= length;
				ez /= length;
			}
			const float distance = ((cx - prevX) * ez - (cz - prevZ) * ex) - half;
			if (distance > 0.0f)
				return false;
			if (bestDistance < distance)
			{
				best = i;
				bestDistance = distance;
			}
			prevX = poly.x[i];
			prevZ = poly.z[i];
		}
		if (-half < bestDistance)
		{
			const int prev = (best > 0 ? best : count) - 1;
			const float px = poly.x[prev], pz = poly.z[prev];
			const float qx = poly.x[best], qz = poly.z[best];
			if ((cx - px) * (qx - px) + (cz - pz) * (qz - pz) > 0.0f)
			{
				if ((cx - qx) * (px - qx) + (cz - qz) * (pz - qz) <= 0.0f)
				{
					const float dx = cx - qx, dz = cz - qz;
					if (half * half < dx * dx + dz * dz)
						return false;
				}
			}
			else
			{
				const float dx = cx - px, dz = cz - pz;
				if (half * half < dx * dx + dz * dz)
					return false;
			}
		}
		return true;
	}

	struct Grid
	{
		int minX = 0;
		int maxX = 0; // exclusive
		int minZ = 0;
		int maxZ = 0; // exclusive
		float size = 0.0f;
		float scale = 0.0f;

		int Width() const noexcept { return maxX - minX; }
		int Depth() const noexcept { return maxZ - minZ; }
		bool Valid() const noexcept
		{
			return Width() > 0 && Depth() > 0 && Width() <= 8192 && Depth() <= 8192 &&
				std::isfinite(size) && size > 0.0f && std::isfinite(scale) && scale > 0.0f;
		}
	};

	// Inclusive cell rectangle, indices relative to the grid minimum.
	struct CellRect
	{
		int x0 = 0;
		int z0 = 0;
		int x1 = -1;
		int z1 = -1;

		bool Empty() const noexcept { return x1 < x0 || z1 < z0; }
		bool Contains(int x, int z) const noexcept { return x >= x0 && x <= x1 && z >= z0 && z <= z1; }
		bool Overlaps(const CellRect& o) const noexcept
		{
			return !Empty() && !o.Empty() && x0 <= o.x1 && o.x0 <= x1 && z0 <= o.z1 && o.z0 <= z1;
		}
		CellRect Intersect(const CellRect& o) const noexcept
		{
			CellRect r;
			r.x0 = x0 > o.x0 ? x0 : o.x0;
			r.z0 = z0 > o.z0 ? z0 : o.z0;
			r.x1 = x1 < o.x1 ? x1 : o.x1;
			r.z1 = z1 < o.z1 ? z1 : o.z1;
			return r;
		}
	};

	inline int ClampInt(int v, int lo, int hi) noexcept
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	// UpdateCells (GOG 0x004690F0): floor(coord * scale) in float, minus the
	// grid minimum, clamped to the grid.
	inline int CellIndex(float coord, float scale, int gridMin, int count) noexcept
	{
		const float scaled = coord * scale;
		const int absolute = static_cast<int>(std::floor(static_cast<double>(scaled)));
		return ClampInt(absolute - gridMin, 0, count - 1);
	}

	inline CellRect RectFromArea(const Grid& g, float x0, float z0, float x1, float z1) noexcept
	{
		if (x1 < x0)
		{
			const float t = x0;
			x0 = x1;
			x1 = t;
		}
		if (z1 < z0)
		{
			const float t = z0;
			z0 = z1;
			z1 = t;
		}
		CellRect r;
		r.x0 = CellIndex(x0, g.scale, g.minX, g.Width());
		r.x1 = CellIndex(x1, g.scale, g.minX, g.Width());
		r.z0 = CellIndex(z0, g.scale, g.minZ, g.Depth());
		r.z1 = CellIndex(z1, g.scale, g.minZ, g.Depth());
		return r;
	}

	// BuildingBlock's cell centre: ((float)absoluteIndex + 0.5) * size.
	inline float CellCentre(int relativeIndex, int gridMin, float size) noexcept
	{
		return (static_cast<float>(relativeIndex + gridMin) + 0.5f) * size;
	}
}
