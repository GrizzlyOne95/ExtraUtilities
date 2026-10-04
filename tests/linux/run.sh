#!/usr/bin/env bash
#
# Host-side Linux checks for Extra Utilities. These do not build exu.dll.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

pass() {
    echo "OK: $*"
}

test_python_tools() {
    python3 "$ROOT/tools/validate_hardening.py"
    python3 "$ROOT/tools/generate_bzr_build_profile.py" --check
    python3 "$ROOT/tools/generate_engine_addresses.py" --check
    python3 "$ROOT/tools/test_bzr_qualification.py"
    pass "Python validation tools"
}

test_native_hud_trace() {
    python3 "$ROOT/tools/test_native_hud_trace.py"
    node --check "$ROOT/tools/native_hud_trace/capture.js"
    node "$ROOT/tools/native_hud_trace/test_capture.js"
    pass "native HUD qualification instrument host checks"
}

# The weather billboard textures are generated, not authored, so an edit to the
# generator that was never re-run would otherwise ship stale art silently.
test_weather_textures() {
    python3 "$ROOT/tools/generate_weather_textures.py" --check
    pass "weather textures are current"
}

# The ring gauge segment art is generated too; same staleness guard.
test_hud_textures() {
    python3 "$ROOT/tools/generate_hud_textures.py" --check
    pass "HUD textures are current"
}

# The scripted ring gauge reaches the game only through the exu table and
# stock Lua globals, so a fake of both checks its layout and fill logic.
test_ring_gauge() {
    local lua
    for lua in lua5.1 lua5.4 lua luajit; do
        if command -v "$lua" >/dev/null 2>&1; then
            (cd "$ROOT" && "$lua" tests/host/ring_gauge_test.lua)                 || fail "ring gauge checks failed"
            pass "ring gauge ($lua)"
            return
        fi
    done
    fail "no Lua interpreter found for tests/host/ring_gauge_test.lua"
}

test_native_hud_controller() {
    local lua
    for lua in lua5.1 lua5.4 lua luajit; do
        if command -v "$lua" >/dev/null 2>&1; then
            (cd "$ROOT" && "$lua" tests/host/native_hud_controller_test.lua) \
                || fail "native HUD controller checks failed"
            pass "native HUD controller ($lua)"
            return
        fi
    done
    fail "no Lua interpreter found for native HUD controller checks"
}

# The weather controller talks to the game only through the global exu table,
# so a fake one exercises every decision it makes with no game present.
test_weather_controller() {
    local lua
    for lua in lua5.1 lua5.4 lua luajit; do
        if command -v "$lua" >/dev/null 2>&1; then
            (cd "$ROOT" && "$lua" tests/host/weather_controller_test.lua) \
                || fail "weather controller checks failed"
            pass "weather controller ($lua)"
            return
        fi
    done
    fail "no Lua interpreter found for tests/host/weather_controller_test.lua"
}

# The in-game pilot FSM capture script only reaches the game through the exu
# table it is handed, so a fake one walks it through a whole session.
test_pilot_fsm_capture() {
    local lua
    for lua in lua5.1 lua5.4 lua luajit; do
        if command -v "$lua" >/dev/null 2>&1; then
            (cd "$ROOT" && "$lua" tests/host/pilot_fsm_capture_test.lua) \
                || fail "pilot FSM capture checks failed"
            pass "pilot FSM capture script ($lua)"
            return
        fi
    done
    fail "no Lua interpreter found for tests/host/pilot_fsm_capture_test.lua"
}

# Ogre ParticleFX colours have already been converted to the render system's
# vertex format. Reusing Redux's native-sprite SM4 BGRA swizzle turns orange
# dust blue, so keep the weather materials on the dedicated no-swizzle path.
test_weather_particle_color_contract() {
    local shader="$ROOT/Workshop/exu_ogre_particle-sm4.hlsl"
    local program="$ROOT/Workshop/exu_ogre_particle.program"
    local material="$ROOT/Workshop/exu_weather.material"

    grep -Fq 'vColor = iColor * diffuseColor;' "$shader" \
        || fail "Ogre particle shader no longer preserves RGBA vertex colour"
    if grep -Eq 'iColor\s*\.\s*bgra' "$shader"; then
        fail "Ogre particle shader reintroduced the native-sprite BGRA swizzle"
    fi
    grep -Fq 'source exu_ogre_particle-sm4.hlsl' "$program" \
        || fail "Ogre particle program no longer references its SM4 source"
    grep -Fq 'material EXU_FX/Dust : EXU_FX/OgreParticleAlphaBlend' "$material" \
        || fail "weather dust no longer uses the Ogre particle colour path"
    pass "weather particle RGBA contract"
}

# Pure logic that has no Windows, Ogre, OpenShim or Lua dependency lives in its
# own headers precisely so it can be compiled and exercised here rather than
# only on a machine with the game installed.
test_host_cpp() {
    local cxx build src
    cxx="${CXX:-g++}"
    if ! command -v "$cxx" >/dev/null 2>&1; then
        fail "no C++ compiler found (set CXX)"
    fi

    build="$(mktemp -d)"
    for src in "$ROOT"/tests/host/*.cpp; do
        local name
        name="$(basename "$src" .cpp)"
        "$cxx" -std=c++17 -Wall -Wextra -Werror -I "$ROOT/src" -o "$build/$name" "$src" \
            || { rm -rf "$build"; fail "compile failed: $src"; }
        "$build/$name" || { rm -rf "$build"; fail "test failed: $name"; }
    done
    rm -rf "$build"
    pass "host C++ checks"
}

# Both installers recognise an existing EXU by the string luaopen_exu prints
# on load; if the string changes they would refuse to update a real EXU.
test_installer_marker() {
    grep -Fq 'lua_pushstring(L, "exu.dll loaded");' "$ROOT/src/luaexport.cpp" \
        || fail "luaexport.cpp no longer prints the installer marker \"exu.dll loaded\""
    local script
    for script in "$ROOT/scripts/install_linux.sh" "$ROOT/scripts/deploy_linux_proton.sh"; do
        grep -Fq 'grep -a -q "exu.dll loaded"' "$script" \
            || fail "$(basename "$script") no longer checks the installer marker"
    done
    pass "installer marker matches exu.dll"
}

# Deploys keep only the newest three exu.dll backups.
test_backup_pruning() {
    local fake stamp
    fake="$(mktemp -d)"
    : >"$fake/exu.dll"
    for stamp in 20260101-000001 20260101-000002 20260101-000003 20260101-000004 20260101-000005; do
        : >"$fake/exu.dll.bak-$stamp"
    done
    : >"$fake/keep-me.txt"
    (
        # shellcheck disable=SC1090
        source <(sed -n '/^prune_exu_backups() {/,/^}/p' "$ROOT/scripts/install_linux.sh")
        prune_exu_backups "$fake/exu.dll"
    )
    local remaining
    remaining="$(cd "$fake" && ls -1 | tr '\n' ' ')"
    rm -rf "$fake"
    [[ "$remaining" == "exu.dll exu.dll.bak-20260101-000003 exu.dll.bak-20260101-000004 exu.dll.bak-20260101-000005 keep-me.txt " ]] \
        || fail "backup pruning left: $remaining"
    pass "installer backup pruning"
}

test_script_syntax() {
    local script
    for script in \
        "$ROOT/install_requirements.sh" \
        "$ROOT/scripts/steam_game_paths.sh" \
        "$ROOT/scripts/deploy_linux_proton.sh" \
        "$ROOT/scripts/install_linux.sh" \
        "$ROOT/tests/linux/run.sh"
    do
        bash -n "$script" || fail "bash -n failed: $script"
    done
    pass "installer and host scripts parse"
}

test_steam_path_override() {
    # shellcheck source=scripts/steam_game_paths.sh
    source "$ROOT/scripts/steam_game_paths.sh"
    local fake
    fake="$(mktemp -d)"
    : >"$fake/battlezone98redux.exe"
    BZR_GAME_PATH="$fake"
    detect_bzr_game_paths
    local ok=0
    [[ ${#BZR_GAME_PATHS[@]} -eq 1 && "${BZR_GAME_PATHS[0]}" == "$fake" ]] && ok=1
    rm -rf "$fake"
    [[ "$ok" -eq 1 ]] || fail "BZR_GAME_PATH override was not honoured"
    pass "Steam path override"
}

test_help_exits_clean() {
    "$ROOT/scripts/install_linux.sh" --help >/dev/null
    "$ROOT/scripts/deploy_linux_proton.sh" --help >/dev/null
    pass "installer --help"
}

# Regression: an empty BZR_GAME_PATHS must report "no install found" rather
# than surviving the flavour filter as an empty path and deploying to /exu.dll.
test_no_install_found() {
    local sandbox installer out rc
    sandbox="$(mktemp -d)"
    installer="$ROOT/scripts/install_linux.sh"
    rc=0
    out="$(env -u STEAM_ROOT -u BZR_GAME_PATH -u EXU_DLL HOME="$sandbox" "$installer" --native 2>&1)" || rc=$?
    rm -rf "$sandbox"
    [[ "$rc" -eq 1 ]] || fail "expected exit 1 with no game installed, got $rc: $out"
    if ! grep -q "no Battlezone 98 Redux install found" <<<"$out"; then
        fail "expected the no-install message, got: $out"
    fi
    if grep -qi "exu.dll" <<<"$out"; then
        fail "installer touched a DLL with no game installed: $out"
    fi
    pass "no-install path reports cleanly"
}

test_bad_game_path_rejected() {
    local sandbox out rc
    sandbox="$(mktemp -d)"
    rc=0
    out="$("$ROOT/scripts/install_linux.sh" --game-path "$sandbox/missing" 2>&1)" || rc=$?
    rm -rf "$sandbox"
    [[ "$rc" -eq 1 ]] || fail "expected exit 1 for a missing --game-path, got $rc: $out"
    if ! grep -q "not a directory" <<<"$out"; then
        fail "expected a directory error, got: $out"
    fi
    pass "--game-path validation"
}

test_python_tools
test_native_hud_trace
test_weather_textures
test_hud_textures
test_host_cpp
test_native_hud_controller
test_ring_gauge
test_weather_controller
test_pilot_fsm_capture
test_weather_particle_color_contract
test_script_syntax
test_installer_marker
test_backup_pruning
test_steam_path_override
test_help_exits_clean
test_no_install_found
test_bad_game_path_rejected

echo "All Linux host checks passed."
