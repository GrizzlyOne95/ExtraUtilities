/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "UI/NativeHudControls.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>

using namespace ExtraUtilities::NativeHud;

struct Fake final : Provider
{
    uint32_t caps = 0;
    bool accept = true;
    Rect response{100, 200, 20, 90};
    std::vector<std::string> calls;
    uint32_t Capabilities() override { return caps; }
    bool GetRect(const char* name, bool stock, Rect& rect) override
    {
        calls.push_back(std::string(stock ? "default:" : "get:") + name);
        rect = response;
        return accept;
    }
    bool SetRect(const char* name, Rect) override { calls.push_back(std::string("set:") + name); return accept; }
    bool SetVisible(const char* name, bool) override { calls.push_back(std::string("visible:") + name); return accept; }
    bool Restore(const char* name) override { calls.push_back(std::string("restore:") + name); return accept; }
};

int main()
{
    Fake fake;
    Controls controls(fake);
    Rect rect{1, 2, 3, 4};
    assert(!controls.Available(Meter::Hull));
    assert(controls.SetRect(Meter::Hull, {1, 2, 3, 4}) == Result::Unavailable);
    assert(!controls.GetRect(Meter::Hull, false, rect));
    assert(controls.SetVisible(Meter::Ammo, false) == Result::Unavailable);
    assert(fake.calls.empty());
    controls.ResetMission();
    assert(fake.calls.empty());
    Meter meter;
    assert(ParseMeter("hull", meter) && meter == Meter::Hull);
    assert(ParseMeter("ammo", meter) && meter == Meter::Ammo);
    assert(!ParseMeter("radar", meter));
    assert(!ParseMeter(std::string_view("hull\0radar", 10), meter));
    assert(controls.SetRect(static_cast<Meter>(99), {}) == Result::InvalidMeter);
    assert(controls.SetRect(Meter::Hull, {INT_MAX, 1, INT_MAX, 1}) == Result::InvalidRect);
    assert(controls.SetRect(Meter::Hull, {1, 1, 0, 1}) == Result::InvalidRect);
    assert(controls.SetRect(Meter::Hull, {1, 1, 1, 16385}) == Result::InvalidRect);
    fake.caps = 1;
    assert(controls.SetRect(Meter::Ammo, {1, 2, 3, 4}) == Result::Unavailable);
    assert(controls.SetRect(Meter::Hull, {-20, 20, 30, 40}) == Result::Accepted);
    assert(controls.GetRect(Meter::Hull, true, rect));
    assert(rect.x == 100 && rect.y == 200 && rect.w == 20 && rect.h == 90);
    fake.response.w = -1;
    assert(!controls.GetRect(Meter::Hull, false, rect));
    assert(rect.w == 20); // malformed provider data never escapes the bridge
    fake.accept = false;
    assert(controls.SetVisible(Meter::Hull, false) == Result::Rejected);
    fake.accept = true;
    fake.calls.clear();
    controls.ResetMission();
    assert(fake.calls == std::vector<std::string>{"restore:hull"}); // no global reset
    fake.caps = 3;
    assert(controls.SetVisible(Meter::Ammo, false) == Result::Accepted);
    assert(controls.SetRect(Meter::Hull, {1, 2, 3, 4}) == Result::Accepted);
    fake.calls.clear();
    assert(controls.Restore(Meter::Hull) == Result::Accepted);
    assert(controls.RestoreOwned() == Result::Accepted);
    assert((fake.calls == std::vector<std::string>{"restore:hull", "restore:ammo"}));
    fake.calls.clear();
    assert(controls.RestoreOwned() == Result::Accepted && fake.calls.empty());
    assert(controls.SetVisible(Meter::Hull, false) == Result::Accepted);
    fake.caps = 0;
    controls.ResetMission(); // module disappeared; forget the old owner
    fake.caps = 3;
    fake.calls.clear();
    controls.ResetMission();
    assert(fake.calls.empty());
    assert(Reason(Result::Accepted) == nullptr);
    std::puts("EXU native HUD control/ownership tests passed");
}
