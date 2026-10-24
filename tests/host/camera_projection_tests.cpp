// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Ogre/CameraProjection.h"
#include "Ogre/OgreRenderSpace.h"
#include <cassert>
#include <limits>

using namespace ExtraUtilities::CameraProjection;
struct Vec { double x, y, z; };
int main()
{
    const Matrix view{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    const Matrix perspective{{{1,0,0,0},{0,1,0,0},{0,0,-1,-2},{0,0,-1,0}}};
    Result out;
    const Viewport viewport{100,50,800,600};
    assert(Project(Vec{0,0,-10},view,perspective,viewport,out));
    assert(out.x == 500 && out.y == 350 && out.depth == 10);
    assert(Project(Vec{10,10,-10},view,perspective,viewport,out));
    assert(out.x == 900 && out.y == 50);
    assert(Project(Vec{20,0,-10},view,perspective,viewport,out) && out.x > 900);
    assert(Project(Vec{0,0,10},view,perspective,viewport,out) && out.depth < 0);
    assert(!Project(Vec{0,0,0},view,perspective,viewport,out));
    assert(!Project(Vec{0,0,-10},view,perspective,{0,0,0,600},out));
    assert(!Project(Vec{std::numeric_limits<double>::infinity(),0,-10},view,perspective,viewport,out));
    const auto render = ExtraUtilities::OgreRenderSpace::SimPositionToRender(Vec{2560,0,2570},Vec{2560,0,-2560});
    assert(Project(render,view,perspective,viewport,out) && out.x == 500 && out.depth == 10);
    // Orthographic projection must not use W as forward depth.
    assert(Project(Vec{0.5,0.5,-10},view,view,viewport,out));
    assert(out.x == 700 && out.y == 200 && out.depth == 10);
    Matrix translated = view; translated.m[0][3] = -4;
    assert(Project(Vec{4,0,-10},translated,perspective,viewport,out) && out.x == 500);
}
