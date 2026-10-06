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

#include "Game/PathBlockMath.h"
#include "HostTest.h"

#include <cmath>
#include <string>
#include <vector>

using namespace ExtraUtilities::PathBlockMath;
using HostTest::Expect;

namespace
{
	// Outward-wound axis-aligned box as 12 triangles.
	void AddBox(std::vector<Triangle>& out, float x0, float y0, float z0, float x1, float y1, float z1)
	{
		const Vec3f v[8] = {
			{ x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
			{ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 },
		};
		// Quads wound counter-clockwise seen from outside.
		const int quads[6][4] = {
			{ 0, 3, 2, 1 }, // -Z
			{ 4, 5, 6, 7 }, // +Z
			{ 0, 4, 7, 3 }, // -X
			{ 1, 2, 6, 5 }, // +X
			{ 0, 1, 5, 4 }, // -Y
			{ 3, 7, 6, 2 }, // +Y
		};
		for (const auto& q : quads)
		{
			out.push_back({ v[q[0]], v[q[1]], v[q[2]] });
			out.push_back({ v[q[0]], v[q[2]], v[q[3]] });
		}
	}

	int Winding(const std::vector<Triangle>& t, double x, double y, double z)
	{
		return WindingAt(t.data(), t.size(), x, y, z);
	}

	void TestParsing()
	{
		Expect(ParseMode(nullptr) == Mode::Box, "null pathBlock is box");
		Expect(ParseMode("") == Mode::Box, "empty pathBlock is box");
		Expect(ParseMode("\"faces\"") == Mode::Faces, "quoted faces");
		Expect(ParseMode("  FACES ") == Mode::Faces, "faces ignores case and blanks");
		Expect(ParseMode("none") == Mode::None, "none");
		Expect(ParseMode("'None'") == Mode::None, "single-quoted none");
		Expect(ParseMode("box") == Mode::Box, "box");
		Expect(ParseMode("face") == Mode::Box, "unknown value falls back to box");
		Expect(ParseHeight(nullptr, 1.5f) == 1.5f, "missing height uses the default");
		Expect(ParseHeight("\"2.25\"", 1.5f) == 2.25f, "quoted height");
		Expect(ParseHeight("abc", 3.0f) == 3.0f, "bad height uses the default");
		Expect(ParseHeight("nan", 3.0f) == 3.0f, "non-finite height uses the default");
	}

	void TestWinding()
	{
		std::vector<Triangle> box;
		AddBox(box, -1, 0, -1, 1, 2, 1);
		Expect(Winding(box, 0, 1, 0) == -1, "inside an outward box is -1 (only the exit is ahead)");
		Expect(Winding(box, -2, 1, 0) == 0, "in front of the box is 0 (enter + exit)");
		Expect(Winding(box, 2, 1, 0) == 0, "behind the box is 0");
		Expect(Winding(box, 0, 3, 0) == 0, "above the box is 0");
		// Rays through the shared diagonal edges of the box's quads and through
		// its vertices are counted once, never twice or zero times.
		Expect(Winding(box, 0, 1, 0.0) == -1, "ray through a quad diagonal");
		Expect(Winding(box, 0, 0.5, -0.5) == -1, "another point on the diagonal");
		Expect(Winding(box, -2, 2, 1) == 0, "ray along an outer edge never counts");

		std::vector<Triangle> inward;
		for (const Triangle& t : box)
			inward.push_back({ t.a, t.c, t.b });
		Expect(Winding(inward, 0, 1, 0) == 1, "an inward-wound part is +1: still nonzero");

		std::vector<Triangle> overlap = box;
		AddBox(overlap, 0, 0, -1, 2, 2, 1);
		Expect(Winding(overlap, 0.5, 1, 0) == -2, "overlapping parts sum (even-odd would call this empty)");
		Expect(SolidAt(overlap.data(), overlap.size(), 0.5, 0, 1.5, 3.0), "overlap is solid");
		Expect(Winding(overlap, 1.5, 1, 0) == -1, "only the second part");
	}

	// A 32 x 32 m deck, 10 m tall, with a 16 m tunnel along Z at ground level
	// (8 m high) and a 2 m roof slab over it.
	std::vector<Triangle> TunnelDeck()
	{
		std::vector<Triangle> t;
		AddBox(t, -16, 0, -16, -8, 10, 16);
		AddBox(t, 8, 0, -16, 16, 10, 16);
		AddBox(t, -8, 8, -16, 8, 10, 16);
		return t;
	}

	void TestTunnel()
	{
		const std::vector<Triangle> deck = TunnelDeck();
		Expect(!SolidAt(deck.data(), deck.size(), 0, 0, 1.5, 3.0), "tunnel centre is open");
		Expect(!SolidAt(deck.data(), deck.size(), 7.5, 10, 1.5, 3.0), "tunnel edge is open");
		Expect(SolidAt(deck.data(), deck.size(), -12, 0, 1.5, 3.0), "west deck is solid");
		Expect(SolidAt(deck.data(), deck.size(), 12, -10, 1.5, 3.0), "east deck is solid");
		Expect(SolidAt(deck.data(), deck.size(), 0, 0, 9.0, 9.0), "roof slab is solid at its height");

		// Every placement: the open cells inside the corridor form a 4-connected
		// path from one mouth to the other (16 m > 10 m * sqrt 2).
		const float bmin[3] = { -16, 0, -16 };
		const float bmax[3] = { 16, 10, 16 };
		const float size = 10.0f;
		int failures = 0;
		for (int yaw = 0; yaw <= 90; yaw += 5)
		{
			for (int ox = 0; ox < 10; ox += 2)
			{
				for (int oz = 0; oz < 10; oz += 2)
				{
					const double a = yaw * 3.14159265358979 / 180.0;
					Placement p;
					p.rightX = static_cast<float>(std::cos(a));
					p.rightZ = static_cast<float>(-std::sin(a));
					p.frontX = static_cast<float>(std::sin(a));
					p.frontZ = static_cast<float>(std::cos(a));
					p.positX = 100.0 + ox;
					p.positZ = 100.0 + oz;
					const int n = 12;
					bool open[n][n] = {};
					double lzOf[n][n] = {};
					for (int i = 0; i < n; ++i)
					{
						for (int j = 0; j < n; ++j)
						{
							const float cx = CellCentre(i, 4, size);
							const float cz = CellCentre(j, 4, size);
							double lx = 0, lz = 0;
							if (!WorldToLocal(p, cx, cz, lx, lz) || !InsideBox(lx, lz, bmin, bmax))
								continue;
							lzOf[i][j] = lz;
							open[i][j] = std::fabs(lx) < 8.0 && !SolidAt(deck.data(), deck.size(), lx, lz, 1.5, 3.0);
						}
					}
					bool seen[n][n] = {};
					int stack[n * n][2];
					int top = 0;
					for (int i = 0; i < n; ++i)
						for (int j = 0; j < n; ++j)
							if (open[i][j] && lzOf[i][j] < -6.0)
							{
								seen[i][j] = true;
								stack[top][0] = i;
								stack[top][1] = j;
								++top;
							}
					bool reached = false;
					while (top > 0)
					{
						--top;
						const int i = stack[top][0], j = stack[top][1];
						if (lzOf[i][j] > 6.0)
							reached = true;
						const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
						for (const auto& s : d)
						{
							const int u = i + s[0], v = j + s[1];
							if (u >= 0 && v >= 0 && u < n && v < n && open[u][v] && !seen[u][v])
							{
								seen[u][v] = true;
								stack[top][0] = u;
								stack[top][1] = v;
								++top;
							}
						}
					}
					if (!reached)
						++failures;
				}
			}
		}
		Expect(failures == 0, "16 m tunnel has a 4-connected open corridor at every yaw and alignment");
	}

	void TestGridMapping()
	{
		Grid g;
		g.minX = -64;
		g.maxX = 64;
		g.minZ = -32;
		g.maxZ = 32;
		g.size = 10.0f;
		g.scale = 0.1f;
		Expect(CellIndex(0.0f, g.scale, g.minX, g.Width()) == 64, "x = 0 is cell 0 (relative 64)");
		Expect(CellIndex(-0.5f, g.scale, g.minX, g.Width()) == 63, "floor, not truncation");
		Expect(CellIndex(1.0e6f, g.scale, g.minX, g.Width()) == g.Width() - 1, "clamped high");
		Expect(CellIndex(-1.0e6f, g.scale, g.minZ, g.Depth()) == 0, "clamped low");
		const CellRect r = RectFromArea(g, 25.0f, 5.0f, -5.0f, -15.0f);
		Expect(r.x0 == 63 && r.x1 == 66 && r.z0 == 30 && r.z1 == 32, "area rectangle is sorted and floored");
		Expect(CellCentre(64, g.minX, g.size) == 5.0f, "centre of cell 0 is 5 m");
	}

	void TestBoxClone()
	{
		Placement p;
		p.positX = 100.0;
		p.positZ = 200.0;
		const float bmin[3] = { -10, 0, -5 };
		const float bmax[3] = { 10, 4, 5 };
		const BoxPolygon poly = MakeBoxPolygon(p, bmin, bmax);
		const float half = 5.0f;
		Expect(BoxCellBlocked(poly, 100.0f, 200.0f, half), "centre is blocked");
		Expect(BoxCellBlocked(poly, 114.0f, 200.0f, half), "within half a cell of the edge is blocked");
		Expect(!BoxCellBlocked(poly, 116.0f, 200.0f, half), "beyond half a cell is open");
		Expect(!BoxCellBlocked(poly, 114.0f, 209.0f, half), "rounded corner excludes the diagonal");
		Expect(BoxCellBlocked(poly, 112.0f, 207.0f, half), "inside the rounded corner");
	}
}

int main()
{
	TestParsing();
	TestWinding();
	TestTunnel();
	TestGridMapping();
	TestBoxClone();
	return HostTest::Finish("path block math");
}
