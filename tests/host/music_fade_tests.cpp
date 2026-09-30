/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "Util/MusicFade.h"
#include "HostTest.h"
#include <limits>
#include <vector>

using namespace ExtraUtilities::Soundtrack;
using namespace HostTest;

namespace
{
    struct Fake final : Backend
    {
        State state{7, 0, true, false, 10};
        bool exists = true;
        bool readable = true;
        bool playable = true;
        bool volumeWorks = true;
        int plays = 0;
        float gain = 1;
        std::vector<float> gains;
        bool Read(State& out) override { out = state; return readable; }
        bool CanPlay(int) override { return exists; }
        bool Play(int track) override { ++plays; if (!playable) return false; state.track = track; return true; }
        bool SetGain(float next) override
        {
            if (!volumeWorks) return false;
            gain = next; gains.push_back(next); return true;
        }
    };
}

int main()
{
    Fake backend;
    FadeController fade;
    Expect(fade.Fade(backend, 0, 2), "fade starts");
    Expect(fade.Update(backend, 1) && fade.Gain() == 0.5f, "halfway");
    backend.state.paused = true;
    Expect(fade.Update(backend, 100) && fade.Gain() == 0.5f && fade.Active(), "pause freezes fade");
    backend.state.paused = false;
    Expect(fade.Update(backend, 1) && fade.Gain() == 0 && !fade.Active(), "exact silence endpoint");
    Expect(backend.state.started && !backend.state.paused, "silence does not pause or stop");
    Expect(fade.Fade(backend, 1, 0) && fade.Gain() == 1, "instant fade");
    Expect(fade.Change(backend, 12, 2, 4), "change schedules");
    Expect(backend.plays == 0 && backend.state.track == 7, "change waits for fade-out");
    Expect(fade.Update(backend, 4) && backend.plays == 1 && backend.state.track == 12 && fade.Gain() == 0.5f,
        "overshoot enters fade-in halfway");
    Expect(fade.Update(backend, 20) && fade.Gain() == 1 && !fade.Active(), "large update finishes exactly");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    Expect(fade.Fade(backend, 0, 3), "next fade starts");
    Expect(!fade.Fade(backend, 0.5f, nan) && !fade.Change(backend, 8, inf, 1) &&
        !fade.Change(backend, 8, -1, 1) && !fade.Update(backend, -1), "non-finite/negative duration rejected");
    Expect(!fade.Fade(backend, static_cast<float>(nan), 1) && !fade.Fade(backend, 2, 1), "invalid gain rejected");
    Expect(!fade.Change(backend, -1, 0, 0) && !fade.Change(backend, 256, 0, 0), "invalid track rejected");
    backend.exists = false;
    Expect(!fade.Change(backend, 98, 1, 1) && fade.Active() && backend.state.track == 12,
        "missing item preserves current fade and track");
    backend.exists = true;
    Expect(fade.Reset(backend) && !fade.Active() && backend.gain == 1, "reset restores player volume");

    Expect(fade.Change(backend, 8, 1, 1), "deferred switch");
    backend.exists = false;
    const int before = backend.plays;
    Expect(!fade.Update(backend, 1) && backend.plays == before && !fade.Active() && backend.gain == 1,
        "file disappearing during fade-out restores old gain without switching");
    backend.exists = true;
    Expect(fade.Fade(backend, 0.2f, 0), "low gain");
    Expect(fade.Change(backend, 8, 1, 1), "switch from low gain");
    backend.playable = false;
    Expect(!fade.Update(backend, 1) && backend.gain == 0.2f && !fade.Active(), "failed playback recovers prior gain");
    backend.playable = true;
    fade.Reset(backend);
    Expect(fade.Fade(backend, 0, 3), "fade before external selection");
    backend.state.track = 9;
    Expect(fade.Update(backend, 1) && !fade.Active() && backend.gain == 1, "external selection cancels override");
    Expect(fade.Fade(backend, 0, 3), "fade before external release");
    backend.state.slot = -1;
    Expect(fade.Update(backend, 1) && !fade.Active() && backend.gain == 1, "released stream cancels override");
    Expect(fade.Change(backend, 10, 20, 0) && !fade.Active() && backend.state.track == 10,
        "no stream skips fade-out");
    backend.state.slot = 0;
    backend.volumeWorks = false;
    Expect(!fade.Fade(backend, 0, 1), "volume failure rejects fade");

    Expect(BufferVolume(10, 1) == 0, "stock maximum volume");
    Expect(BufferVolume(5, 1) == -800, "stock logarithmic curve");
    Expect(BufferVolume(10, 0.5f) == -800, "relative gain follows curve");
    Expect(BufferVolume(10, 0) == -10000 && BufferVolume(0, 1) == -10000, "safe exact silence");
    Expect(BufferVolume(1, 0.0000001f) == -10000, "attenuation clamps to DirectSound minimum");
    return Finish("music fade");
}
