// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/VehicleControlPolicy.h"
#include "HostTest.h"
#include <cstring>
#include <limits>
#include <string>

int main()
{
    using namespace ExtraUtilities::VehicleControl;
    using HostTest::Expect;
    Policy<2> p;
    Command drive{};
    drive.inputs.throttle = 1; drive.inputs.jump = 1; drive.fire = true;
    drive.selectWeapons = true; drive.weaponMask = 3;
    Expect(!p.Set(0x100001,drive,10),"unclaimed vehicle cannot receive controls");
    Expect(!p.Claim(0,1,2,10),"zero handle rejected");
    Expect(p.Claim(0x100001,1,2,10),"live identity can be claimed");
    Expect(!p.Find(0x100001)->command.fire,"new claim starts neutral");
    Expect(p.Set(0x100001,drive,10.1f),"complete command accepted");
    const auto deadline = p.Find(0x100001)->deadline;
    Expect(p.Claim(0x100001,1,2,10.2f),"same identity claim is idempotent");
    Expect(p.Find(0x100001)->deadline == deadline,"claim does not silently renew command lease");
    Expect(!p.Claim(0x100001,3,2,10.2f),"replaced process cannot inherit a claim");
    Expect(!p.Claim(0x100001,1,3,10.2f),"changed process type cannot inherit a claim");
    Expect(p.Find(0x100002) == nullptr,"new serial in reused arena slot cannot inherit control");
    auto bad = drive; bad.inputs.steer = std::numeric_limits<float>::quiet_NaN();
    Expect(!p.Set(0x100001,bad,10.2f),"NaN is rejected atomically");
    Expect(p.Find(0x100001)->command.inputs.jump == 1 && p.Find(0x100001)->deadline == deadline,
        "invalid command leaves old command and expiry unchanged");
    bad = drive; bad.inputs.throttle = 1.01f; Expect(!Valid(bad),"axes bounded to normal input range");
    bad = drive; bad.inputs.turbo = 0x3F800000; Expect(!Valid(bad),"float bit pattern is not an integer boolean");
    bad = drive; bad.inputs.jump = -1; Expect(!Valid(bad),"jump must be integer 0/1");
    bad = drive; bad.weaponMask = 32; Expect(!Valid(bad),"weapon mask is five bits");
    bad = drive; bad.timeout = std::numeric_limits<float>::infinity(); Expect(!Valid(bad),"infinite lease rejected");
    bad = drive; bad.timeout = 0; Expect(!Valid(bad),"zero lease rejected");
    bad = drive; bad.timeout = 5.1f; Expect(!Valid(bad),"lease has an upper bound");
    Expect(!p.Find(0x100001)->Expired(10.3f),"unchanged paused simulation time preserves command");
    Expect(p.Find(0x100001)->Expired(deadline),"lease expires at exact deadline");
    Expect(p.Find(0x100001)->Expired(0),"save/load clock rewind invalidates lease");
    Expect(p.Find(0x100001)->Expired(std::numeric_limits<float>::quiet_NaN()),"invalid engine time fails closed");
    Expect(!p.Set(0x100001,drive,deadline),"expired control needs explicit re-claim");
    Expect(p.Claim(0x200001,4,2,10),"second vehicle independent");
    Expect(!p.Claim(0x300001,5,2,10),"fixed capacity fails without evicting another vehicle");
    p.Release(0x100001); p.Release(0x100001);
    Expect(!p.Find(0x100001) && p.Find(0x200001),"release idempotent and isolated");
    Expect(p.Claim(0x100002,6,2,11),"reused slot can be claimed explicitly");
    Expect(!p.Find(0x100002)->command.fire,"reused slot starts without old firing request");
    p.Reset();
    Expect(!p.Find(0x100002) && !p.Find(0x200001),"mission reset removes every claim");
    Expect(std::strcmp(ProcessStateName(7),"ATTACK") == 0 && std::strcmp(TaskStateName(7),"SLIDE") == 0,
        "process and task enums remain distinct");
    Expect(std::strcmp(TaskStateName(5),"ATTACK") == 0,"task attack enum");
    Expect(std::strcmp(ProcessStateName(-1),"UNKNOWN") == 0 && std::strcmp(TaskStateName(99),"UNKNOWN") == 0,
        "unrecognized states cannot index out of bounds");
    struct Sink
    {
        Inputs applied{};
        std::string events;
        void SetInputs(const Inputs& i) { applied = i; events += 'I'; }
        void Clear() { applied = {}; events += 'C'; }
        void Select(std::uint32_t mask) { events += mask == 3 ? 'W' : '?'; }
        void Fire() { events += 'F'; }
        void Special() { events += 'S'; }
    } sink;
    p.Claim(1,2,3,0);
    drive.special = true; p.Set(1,drive,0);
    Expect(Tick(p,1,2,3,0.1f,sink),"claimed command replaces stock process execution");
    Expect(sink.events == "IWFS","movement then selection then held fire/special");
    Expect(sink.applied.jump == 1 && p.Find(1)->applications == 1,"jump reaches adapter and telemetry advances");
    drive.fire = false; drive.special = false; p.Set(1,drive,0.2f); sink.events.clear();
    Expect(Tick(p,1,2,3,0.3f,sink) && sink.events == "IW","fire=false still replaces AI; no stock firing fallback");
    sink.events.clear();
    Expect(!Tick(p,1,2,3,1,sink) && sink.events == "C" && sink.applied.jump == 0,"expiry clears held jump before resuming stock AI");
    sink.events.clear();
    Expect(!Tick(p,1,2,3,1,sink) && sink.events.empty(),"released vehicle passes directly to stock without writes");
    p.Claim(1,2,3,1); sink.events.clear();
    Expect(!Tick(p,1,9,3,1.1f,sink) && sink.events.empty() && !p.Find(1),"replacement process invalidates control without modifying new object");
    struct ReentrantSink : Sink
    {
        Policy<2>& owner;
        explicit ReentrantSink(Policy<2>& p) : owner(p) {}
        void Fire() { Sink::Fire(); owner.Release(1); owner.Claim(1,2,3,2); }
    } reentrant(p);
    p.Claim(1,2,3,2); drive.fire = true; drive.special = true; p.Set(1,drive,2);
    Expect(Tick(p,1,2,3,2.1f,reentrant) && reentrant.events == "IWF","release/re-claim during weapon callback cancels remaining old actions");
    Expect(p.Find(1)->applications == 0 && !p.Find(1)->command.fire,"callback-created claim is not resurrected or counted by old tick");
    return HostTest::Finish("vehicle control");
}
