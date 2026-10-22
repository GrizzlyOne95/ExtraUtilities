#include "Game/SurfacePointMath.h"
#include "Ogre/SurfaceMaterialPolicy.h"
#include "test_check.h"
#include <cmath>
#include <limits>

int main()
{
    using ExtraUtilities::SurfacePointMath::ToLocal;
    using namespace OpenShimTest;
    const float origin[3]={100,20,-50}, identity[4]={1,0,0,0};
    const float scale[3]={2,3,.5f}, point[3]={104,29,-48};
    float out[3];
    Require(ToLocal(point,origin,identity,scale,out), "identity ToLocal succeeds");
    Check(std::fabs(out[0]-2)<.0001f && std::fabs(out[1]-3)<.0001f && std::fabs(out[2]-4)<.0001f, "identity ToLocal value");
    const float quarterTurn[4]={.70710678f,0,.70710678f,0};
    const float rotated[3]={102,29,-54};
    Require(ToLocal(rotated,origin,quarterTurn,scale,out), "rotated ToLocal succeeds");
    Check(std::fabs(out[0]-2)<.0001f && std::fabs(out[1]-3)<.0001f && std::fabs(out[2]-4)<.0001f, "rotated ToLocal value");
    const float flat[3]={1,0,1}, badQ[4]={0,0,0,0};
    Check(!ToLocal(point,origin,identity,flat,out), "degenerate scale rejected");
    Check(!ToLocal(point,origin,badQ,scale,out), "zero quaternion rejected");
    const float bad[3]={std::numeric_limits<float>::infinity(),0,0};
    Check(!ToLocal(bad,origin,identity,scale,out), "non-finite point rejected");

    // Native chunk namespace reservation.
    using namespace ExtraUtilities::SurfaceMaterialPolicy;
    Check(IsNativeChunkName("exu_surface_chunk_0"), "chunk name reserved");
    Check(IsNativeChunkName("exu_surface_chunk_"), "bare chunk prefix reserved");
    Check(!IsNativeChunkName("exu_surface_chunk"), "short prefix not reserved");
    Check(!IsNativeChunkName("exu_surface_hull"), "Lua clone names stay open");
    Check(!IsNativeChunkName(""), "empty name not reserved");

    // Bounded negative cache: each name is remembered once, capacity is hard.
    BoundedNameSet failed(2);
    Check(!failed.Contains("a"), "cache starts empty");
    Check(failed.Add("a") && failed.Contains("a"), "add remembers");
    Check(failed.Add("a") && failed.Size() == 1, "re-add is idempotent");
    Check(failed.Add("b") && !failed.Add("c") && !failed.Contains("c"), "capacity bound");
    failed.Erase("a");
    Check(!failed.Contains("a") && failed.Add("c"), "erase frees a slot");
    failed.Clear();
    Check(failed.Size() == 0 && !failed.Contains("b"), "clear empties the cache");
    return ExitCode();
}
