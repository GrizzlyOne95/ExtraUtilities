// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/PlayerControlPolicy.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace ExtraUtilities::PlayerControlPolicy;

int main()
{
    // Declarative native field contract, independent of the clearing ranges.
    std::array<std::uint32_t,56> fields{};
    for (auto i : {4,5,6,7,8,9}) fields[i] = Throttle;
    for (auto i : {12,13,14,15,16,17}) fields[i] = Strafe;
    for (auto i : {20,21,22,23,24,25,26,28,29,30,31,32,33,34,35}) fields[i] = Look;
    for (auto i : {36,37,40}) fields[i] = Fire;
    for (auto i : {38,39,41,42,43,44,45}) fields[i] = Weapons;
    fields[46]=Turbo; fields[47]=Jump; fields[48]=Deploy;
    fields[49]=Eject; fields[50]=Abandon; fields[53]=Nav; fields[54]=Jetpack;
    const std::array<std::uint32_t,11> gasFields = {Look,Look,Strafe,Throttle,Turbo,Jump,Eject,Deploy,Abandon,0,Jetpack};
    for (std::uint32_t mask=0; mask<=All; ++mask)
    {
        Inputs input{};
        for (unsigned i=0;i<input.size();++i) input[i]=static_cast<std::uint8_t>(i+1);
        Gas gas{}; gas.fill(1.25f);
        Suppress(input,gas,mask);
        for (unsigned i=0;i<input.size();++i)
            assert(input[i] == ((fields[i]&mask) ? 0 : i+1));
        for (unsigned i=0;i<gas.size();++i)
            assert(gas[i] == ((gasFields[i]&mask) ? 0.f : 1.25f));
        // Idempotent per-simulation filtering; stale held input stays suppressed.
        const auto once = input; const auto gasOnce = gas;
        Suppress(input,gas,mask); assert(input==once && gas==gasOnce);
    }
    Owners owners;
    assert(owners.Set("mission",All));
    assert(owners.Set("cinematic",Fire|Eject));
    assert(owners.Count()==2 && owners.Combined()==All);
    assert(owners.Set("mission",0));
    assert(owners.Combined()==(Fire|Eject)); // release cannot unlock another owner
    assert(owners.Set("cinematic",Jump));
    assert(owners.Count()==1 && owners.Combined()==Jump); // replace, not accumulate
    assert(owners.Set("absent",0));
    assert(!owners.Set("",All));
    assert(!owners.Set(std::string(64,'x'),All));
    assert(!owners.Set(std::string_view("bad\0owner",9),All));
    assert(!owners.Set("badmask",All+1));
    assert(owners.Count()==1 && owners.Combined()==Jump);
    owners.Reset(); assert(owners.Count()==0 && owners.Combined()==0);
    for (int i=0;i<16;++i) assert(owners.Set(std::to_string(i),Fire));
    assert(!owners.Set("overflow",All));
    assert(owners.Set("5",Eject)); // existing owners can update at capacity
    assert(owners.Combined()==(Fire|Eject));
    assert(owners.Set("5",0) && owners.Set("replacement",Jump));
    assert(owners.Count()==16 && owners.Combined()==(Fire|Jump));
    std::cout << "PASS: 4096 channel combinations, UI/key preservation, gas smoothing, owner isolation/capacity/reset\n";
}
