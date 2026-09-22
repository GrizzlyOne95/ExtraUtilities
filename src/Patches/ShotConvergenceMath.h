#pragma once

#include <cmath>

namespace ExtraUtilities::ShotConvergenceMath
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Matrix
    {
        float rightX = 1.0f;
        float rightY = 0.0f;
        float rightZ = 0.0f;
        float upX = 0.0f;
        float upY = 1.0f;
        float upZ = 0.0f;
        float frontX = 0.0f;
        float frontY = 0.0f;
        float frontZ = 1.0f;
        double positionX = 0.0;
        double positionY = 0.0;
        double positionZ = 0.0;
    };

    inline constexpr float kDirectionEpsilon = 0.001f;

    inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
    {
        return {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }

    inline float Dot(const Vec3& lhs, const Vec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    inline bool Normalize(Vec3& value)
    {
        const float lengthSquared = Dot(value, value);
        if (!std::isfinite(lengthSquared) ||
            lengthSquared <= kDirectionEpsilon * kDirectionEpsilon)
        {
            return false;
        }

        const float invLength = 1.0f / std::sqrt(lengthSquared);
        value.x *= invLength;
        value.y *= invLength;
        value.z *= invLength;
        return true;
    }

    inline Matrix Multiply(const Matrix& lhs, const Matrix& rhs)
    {
        Matrix out = {};
        out.rightX = lhs.rightX * rhs.rightX + lhs.rightY * rhs.upX + lhs.rightZ * rhs.frontX;
        out.rightY = lhs.rightX * rhs.rightY + lhs.rightY * rhs.upY + lhs.rightZ * rhs.frontY;
        out.rightZ = lhs.rightX * rhs.rightZ + lhs.rightY * rhs.upZ + lhs.rightZ * rhs.frontZ;
        out.upX = lhs.upX * rhs.rightX + lhs.upY * rhs.upX + lhs.upZ * rhs.frontX;
        out.upY = lhs.upX * rhs.rightY + lhs.upY * rhs.upY + lhs.upZ * rhs.frontY;
        out.upZ = lhs.upX * rhs.rightZ + lhs.upY * rhs.upZ + lhs.upZ * rhs.frontZ;
        out.frontX = lhs.frontX * rhs.rightX + lhs.frontY * rhs.upX + lhs.frontZ * rhs.frontX;
        out.frontY = lhs.frontX * rhs.rightY + lhs.frontY * rhs.upY + lhs.frontZ * rhs.frontY;
        out.frontZ = lhs.frontX * rhs.rightZ + lhs.frontY * rhs.upZ + lhs.frontZ * rhs.frontZ;

        out.positionX =
            static_cast<double>(rhs.rightX) * lhs.positionX +
            static_cast<double>(rhs.upX) * lhs.positionY +
            static_cast<double>(rhs.frontX) * lhs.positionZ + rhs.positionX;
        out.positionY =
            static_cast<double>(rhs.rightY) * lhs.positionX +
            static_cast<double>(rhs.upY) * lhs.positionY +
            static_cast<double>(rhs.frontY) * lhs.positionZ + rhs.positionY;
        out.positionZ =
            static_cast<double>(rhs.rightZ) * lhs.positionX +
            static_cast<double>(rhs.upZ) * lhs.positionY +
            static_cast<double>(rhs.frontZ) * lhs.positionZ + rhs.positionZ;
        return out;
    }

    inline Matrix InvertRigid(const Matrix& value)
    {
        Matrix out = {};
        out.rightX = value.rightX;
        out.rightY = value.upX;
        out.rightZ = value.frontX;
        out.upX = value.rightY;
        out.upY = value.upY;
        out.upZ = value.frontY;
        out.frontX = value.rightZ;
        out.frontY = value.upZ;
        out.frontZ = value.frontZ;

        out.positionX = -(static_cast<double>(value.rightX) * value.positionX +
                          static_cast<double>(value.rightY) * value.positionY +
                          static_cast<double>(value.rightZ) * value.positionZ);
        out.positionY = -(static_cast<double>(value.upX) * value.positionX +
                          static_cast<double>(value.upY) * value.positionY +
                          static_cast<double>(value.upZ) * value.positionZ);
        out.positionZ = -(static_cast<double>(value.frontX) * value.positionX +
                          static_cast<double>(value.frontY) * value.positionY +
                          static_cast<double>(value.frontZ) * value.positionZ);
        return out;
    }

    inline Matrix BuildDirectionalMatrix(const Vec3& origin, Vec3 direction)
    {
        Matrix out = {};
        if (!Normalize(direction))
            return out;

        Vec3 right;
        if (direction.x * direction.x + direction.z * direction.z >= 0.02f)
        {
            right = Cross({ 0.0f, 1.0f, 0.0f }, direction);
            if (!Normalize(right))
                right = { 1.0f, 0.0f, 0.0f };
        }
        else
        {
            right = { 1.0f, 0.0f, 0.0f };
        }

        const Vec3 up = Cross(direction, right);
        out.rightX = right.x; out.rightY = right.y; out.rightZ = right.z;
        out.upX = up.x; out.upY = up.y; out.upZ = up.z;
        out.frontX = direction.x; out.frontY = direction.y; out.frontZ = direction.z;
        out.positionX = origin.x; out.positionY = origin.y; out.positionZ = origin.z;
        return out;
    }

    inline bool SolveReticleConvergence(
        const Matrix& mountLocal,
        const Matrix& mountWorld,
        const Vec3& target,
        Matrix& outMountLocal)
    {
        const Matrix world = Multiply(mountLocal, mountWorld);
        const Vec3 muzzle = {
            static_cast<float>(world.positionX),
            static_cast<float>(world.positionY),
            static_cast<float>(world.positionZ),
        };
        Vec3 direction = {
            target.x - muzzle.x,
            target.y - muzzle.y,
            target.z - muzzle.z,
        };
        if (!Normalize(direction))
            return false;

        const Matrix aimedWorld = BuildDirectionalMatrix(muzzle, direction);
        Matrix aimedLocal = Multiply(aimedWorld, InvertRigid(mountWorld));

        // The stock BZ1/BZR aim routines preserve the hardpoint translation and
        // only rotate the weapon within its mount frame.
        aimedLocal.positionX = mountLocal.positionX;
        aimedLocal.positionY = mountLocal.positionY;
        aimedLocal.positionZ = mountLocal.positionZ;

        outMountLocal = aimedLocal;
        return true;
    }
}
