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
    python3 "$ROOT/tools/test_bzr_qualification.py"
    pass "Python validation tools"
}

test_script_syntax() {
    local script
    for script in \
        "$ROOT/setup-dev.sh" \
        "$ROOT/install_requirements.sh" \
        "$ROOT/scripts/steam_game_paths.sh" \
        "$ROOT/scripts/deploy_linux_proton.sh" \
        "$ROOT/scripts/install_linux.sh" \
        "$ROOT/tests/linux/run.sh"
    do
        bash -n "$script" || fail "bash -n failed: $script"
    done
    pass "installer and setup scripts parse"
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

test_python_tools
test_script_syntax
test_steam_path_override
test_help_exits_clean

echo "All Linux host checks passed."
