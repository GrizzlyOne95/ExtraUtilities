// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <cmath>

// Pure projection math. Ogre's non-render-system projection has the same
// [-1,1] depth convention on D3D9 and D3D11; do not use the RS-adjusted matrix.
namespace ExtraUtilities::CameraProjection
{
    struct Matrix { float m[4][4]; };
    struct Viewport { int left, top, width, height; };
    struct Result { double x = 0, y = 0, depth = 0; };

    template<class Vector>
    bool Project(const Vector& position, const Matrix& view, const Matrix& projection,
        const Viewport& viewport, Result& out) noexcept
    {
        out = {};
        if (viewport.width <= 0 || viewport.height <= 0) return false;
        const double p[4] = {position.x, position.y, position.z, 1};
        double eye[4] = {}, clip[4] = {};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) eye[row] += view.m[row][col] * p[col];
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) clip[row] += projection.m[row][col] * eye[col];
        for (double value : clip) if (!std::isfinite(value)) return false;
        // No stable screen coordinate on the eye plane. Negative W is retained
        // for off-screen directional markers; visibility is a separate query.
        if (std::abs(clip[3]) < 1e-8) return false;
        out.x = viewport.left + (clip[0] / clip[3] + 1) * 0.5 * viewport.width;
        out.y = viewport.top + (1 - clip[1] / clip[3]) * 0.5 * viewport.height;
        out.depth = -eye[2]; // signed camera-forward distance in render/world units
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.depth);
    }
}
