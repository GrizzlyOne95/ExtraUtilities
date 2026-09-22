#include "Patches/ShotConvergenceMath.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace ExtraUtilities::ShotConvergenceMath;

namespace
{
    int gFailures = 0;

    void ExpectNear(float actual, float expected, float tolerance, const char* what)
    {
        if (std::fabs(actual - expected) > tolerance)
        {
            std::cerr << "FAIL: " << what << " expected " << expected << ", got " << actual << '\n';
            ++gFailures;
        }
    }

    Matrix MakeYaw(float radians, double x, double y, double z)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Matrix out = {};
        out.rightX = c;
        out.rightZ = -s;
        out.frontX = s;
        out.frontZ = c;
        out.positionX = x;
        out.positionY = y;
        out.positionZ = z;
        return out;
    }

    float AngleDegrees(Vec3 lhs, Vec3 rhs)
    {
        if (!Normalize(lhs) || !Normalize(rhs))
            return 180.0f;
        float cosine = Dot(lhs, rhs);
        if (cosine > 1.0f) cosine = 1.0f;
        if (cosine < -1.0f) cosine = -1.0f;
        return std::acos(cosine) * 57.2957795f;
    }

    void TestUsesWorldMuzzleAndWritesMountLocalResult()
    {
        const Matrix mountWorld = MakeYaw(0.9f, 2500.0, 6.0, 2500.0);
        Matrix mountLocal = {};
        mountLocal.positionX = 1.5;
        mountLocal.positionY = 0.4;

        const Vec3 target{ 2560.0f, 2.4f, 2615.0f };
        Matrix solved = {};
        if (!SolveReticleConvergence(mountLocal, mountWorld, target, solved))
        {
            std::cerr << "FAIL: solver rejected normal target\n";
            ++gFailures;
            return;
        }

        const Matrix fireWorld = Multiply(solved, mountWorld);
        const Vec3 muzzle{
            static_cast<float>(fireWorld.positionX),
            static_cast<float>(fireWorld.positionY),
            static_cast<float>(fireWorld.positionZ),
        };
        const Vec3 fired{ fireWorld.frontX, fireWorld.frontY, fireWorld.frontZ };
        const Vec3 desired{ target.x - muzzle.x, target.y - muzzle.y, target.z - muzzle.z };

        ExpectNear(AngleDegrees(fired, desired), 0.0f, 0.05f,
            "recomposed fire direction passes through reticle target");
        ExpectNear(static_cast<float>(solved.positionX), static_cast<float>(mountLocal.positionX), 0.0001f,
            "mount-local X translation is preserved");
        ExpectNear(static_cast<float>(solved.positionY), static_cast<float>(mountLocal.positionY), 0.0001f,
            "mount-local Y translation is preserved");
        ExpectNear(static_cast<float>(solved.positionZ), static_cast<float>(mountLocal.positionZ), 0.0001f,
            "mount-local Z translation is preserved");
    }

    void TestOldMountLocalAsWorldOriginMathIsWrong()
    {
        const Matrix mountWorld = MakeYaw(0.9f, 2500.0, 6.0, 2500.0);
        Matrix mountLocal = {};
        mountLocal.positionX = 1.5;
        mountLocal.positionY = 0.4;
        const Vec3 target{ 2560.0f, 2.4f, 2615.0f };

        const Vec3 wrongOrigin{
            static_cast<float>(mountLocal.positionX),
            static_cast<float>(mountLocal.positionY),
            static_cast<float>(mountLocal.positionZ),
        };
        Matrix wrongLocal = BuildDirectionalMatrix(
            wrongOrigin,
            { target.x - wrongOrigin.x, target.y - wrongOrigin.y, target.z - wrongOrigin.z });

        const Matrix wrongWorld = Multiply(wrongLocal, mountWorld);
        const Vec3 muzzle{
            static_cast<float>(wrongWorld.positionX),
            static_cast<float>(wrongWorld.positionY),
            static_cast<float>(wrongWorld.positionZ),
        };
        const Vec3 fired{ wrongWorld.frontX, wrongWorld.frontY, wrongWorld.frontZ };
        const Vec3 desired{ target.x - muzzle.x, target.y - muzzle.y, target.z - muzzle.z };

        if (AngleDegrees(fired, desired) < 20.0f)
        {
            std::cerr << "FAIL: old mount-local-as-world-origin regression case no longer fails\n";
            ++gFailures;
        }
    }
}

int main()
{
    TestUsesWorldMuzzleAndWritesMountLocalResult();
    TestOldMountLocalAsWorldOriginMathIsWrong();

    if (gFailures != 0)
        return EXIT_FAILURE;

    std::cout << "All shot-convergence math checks passed.\n";
    return EXIT_SUCCESS;
}
