#!/usr/bin/env bash
#
# deploy_linux_proton.sh — Copy a Win32 exu.dll into Battlezone 98 Redux
# installs running under Steam Proton (native, Flatpak, or Snap Steam).
#
# Extra Utilities is a Win32 Lua C module. Linux does not produce a native .so.
# Build Release | x86 on Windows, then deploy from Linux:
#   ./scripts/deploy_linux_proton.sh [GAME_DIR] [DLL_PATH]
#
set -euo pipefail

APPID=301650
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=scripts/steam_game_paths.sh
source "$SCRIPT_DIR/steam_game_paths.sh"

usage() {
    cat <<EOF
Usage:
  $0 [GAME_DIR] [DLL_PATH]

Environment:
  BZR_GAME_PATH   Deploy to this directory only (overrides auto-detect)
  STEAM_ROOT      Extra Steam root to scan for libraryfolders.vdf entries

Defaults:
  DLL_PATH        $REPO_ROOT/Release/exu.dll
EOF
}

GAME_DIR="${1:-}"
DLL="${2:-$REPO_ROOT/Release/exu.dll}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ -n "$GAME_DIR" ]]; then
    if [[ ! -d "$GAME_DIR" ]]; then
        echo "error: GAME_DIR is not a directory: $GAME_DIR" >&2
        exit 1
    fi
    BZR_GAME_PATH="$GAME_DIR"
fi

detect_bzr_game_paths

if [[ ${#BZR_GAME_PATHS[@]} -eq 0 ]]; then
    echo "error: could not find Battlezone 98 Redux (AppID $APPID)." >&2
    echo "Install the game in Steam, or pass the game directory explicitly." >&2
    echo >&2
    echo "Typical paths:" >&2
    echo "  Native:  ~/.local/share/Steam/steamapps/common/Battlezone 98 Redux" >&2
    echo "  Flatpak: ~/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Battlezone 98 Redux" >&2
    echo "  Snap:    ~/snap/steam/common/.local/share/Steam/steamapps/common/Battlezone 98 Redux" >&2
    exit 1
fi

if [[ ! -f "$DLL" ]]; then
    echo "error: exu.dll not found: $DLL" >&2
    echo "Build Release | x86 on Windows, copy Release/exu.dll here, or pass the DLL path." >&2
    exit 1
fi

is_exu_dll() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "exu.dll loaded" "$path"
}

deploy_one() {
    local game_dir="$1"
    local dest="$game_dir/exu.dll"

    if [[ -f "$dest" ]] && ! is_exu_dll "$dest"; then
        echo "error: refusing to overwrite non-EXU exu.dll in $game_dir" >&2
        echo "Remove or rename that file first if you intend to replace it." >&2
        return 1
    fi

    echo "Installing Extra Utilities to: $game_dir"
    local stamp
    stamp="$(date +%Y%m%d-%H%M%S)"
    if [[ -f "$dest" ]]; then
        cp -f "$dest" "$dest.bak-$stamp"
    fi
    cp -f "$DLL" "$dest"
    echo "  deployed exu.dll ($(stat -c %s "$dest") bytes)"
}

for game_dir in "${BZR_GAME_PATHS[@]}"; do
    deploy_one "$game_dir"
    echo
done

cat <<'EOF'
Install complete.

exu.dll is a Win32 Lua module. Proton loads it as a Windows DLL; no
WINEDLLOVERRIDES entry is required (unlike OpenShim's winmm.dll proxy).
EOF
