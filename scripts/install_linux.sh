#!/usr/bin/env bash
#
# One-line Linux / Proton installer. Paste from the README:
#   curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/ExtraUtilities/main/scripts/install_linux.sh | bash -s -- --native
#   curl -fsSL https://raw.githubusercontent.com/GrizzlyOne95/ExtraUtilities/main/scripts/install_linux.sh | bash -s -- --snap
#
# Downloads the matched GitHub release exu.dll.
#
set -euo pipefail

REPO_SLUG="${EXU_REPO:-GrizzlyOne95/ExtraUtilities}"
REF="${EXU_REF:-main}"
FLAVOR="all"
GAME_PATH="${BZR_GAME_PATH:-}"
DLL_PATH="${EXU_DLL:-}"

usage() {
    cat <<EOF
Usage:
  install_linux.sh [--native | --snap] [--game-path DIR] [--dll FILE] [--ref git-ref]

    --native      Native Steam and Flatpak installs only
    --snap        Snap Steam installs only
    --game-path   One game directory (overrides flavour filter)
    --dll         Advanced: Win32 exu.dll from a known-good tree
    --ref         Git ref used only to fetch steam_game_paths.sh (default: $REF)

Environment:
  EXU_REPO / EXU_REF / EXU_DLL / BZR_GAME_PATH
EOF
}

validate_ref() {
    local ref="$1"
    if [[ -z "$ref" || ! "$ref" =~ ^[A-Za-z0-9._/-]+$ ]]; then
        echo "Refusing git ref '$ref'." >&2
        exit 1
    fi
    case "$ref" in
        -*|*..*|*//*|*/) echo "Refusing malformed git ref '$ref'." >&2; exit 1 ;;
    esac
}

download_to() {
    local url="$1"
    local out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$out"
        return
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
        return
    fi
    echo "Missing curl or wget." >&2
    exit 2
}

is_snap_game() {
    [[ "$1" == "$HOME/snap/steam/"* ]]
}

filter_flavor() {
    local flavor="$1"
    local kept=()
    local path
    for path in "${BZR_GAME_PATHS[@]:-}"; do
        case "$flavor" in
            all) kept+=("$path") ;;
            snap) is_snap_game "$path" && kept+=("$path") ;;
            native) is_snap_game "$path" || kept+=("$path") ;;
        esac
    done
    if [[ ${#kept[@]} -gt 0 ]]; then
        BZR_GAME_PATHS=("${kept[@]}")
    else
        BZR_GAME_PATHS=()
    fi
}

is_exu_dll() {
    local path="$1"
    [[ -f "$path" ]] && grep -a -q "exu.dll loaded" "$path"
}

download_matched_release() {
    local dest="$1"
    mkdir -p "$dest"
    local base="https://github.com/${REPO_SLUG}/releases/latest/download"
    echo "Downloading matched release DLL from $REPO_SLUG ..."
    if download_to "$base/exu.dll" "$dest/exu.dll" && [[ -s "$dest/exu.dll" ]]; then
        return 0
    fi
    rm -f "$dest/exu.dll"
    return 1
}

deploy_matched() {
    local game_dir="$1" dll="$2"
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
    cp -f "$dll" "$dest"
    echo "  deployed exu.dll ($(stat -c %s "$dest") bytes)"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --native) FLAVOR="native"; shift ;;
        --snap) FLAVOR="snap"; shift ;;
        --game-path)
            [[ $# -ge 2 ]] || { echo "Missing value for --game-path" >&2; exit 1; }
            GAME_PATH="$2"
            shift 2
            ;;
        --dll)
            [[ $# -ge 2 ]] || { echo "Missing value for --dll" >&2; exit 1; }
            DLL_PATH="$2"
            shift 2
            ;;
        --ref)
            [[ $# -ge 2 ]] || { echo "Missing value for --ref" >&2; exit 1; }
            REF="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

validate_ref "$REF"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

src=""
script_dir=""
if [[ -n "${BASH_SOURCE[0]:-}" ]]; then
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd || true)"
fi
if [[ -n "$script_dir" && -f "$script_dir/steam_game_paths.sh" ]]; then
    src="$script_dir"
else
    download_to "https://raw.githubusercontent.com/${REPO_SLUG}/${REF}/scripts/steam_game_paths.sh" \
        "$work/steam_game_paths.sh"
    src="$work"
fi

# shellcheck source=scripts/steam_game_paths.sh
source "$src/steam_game_paths.sh"

if [[ -n "$GAME_PATH" ]]; then
    BZR_GAME_PATH="$GAME_PATH"
fi
detect_bzr_game_paths
if [[ -z "$GAME_PATH" ]]; then
    filter_flavor "$FLAVOR"
fi

if [[ ${#BZR_GAME_PATHS[@]} -eq 0 ]]; then
    echo "error: no Battlezone 98 Redux install found for this Steam flavour." >&2
    case "$FLAVOR" in
        native) echo "Use the Snap paste command if you installed Steam from Snap." >&2 ;;
        snap) echo "Use the Native/Flatpak paste command if you are not on Snap Steam." >&2 ;;
    esac
    exit 1
fi

dll=""

if [[ -n "$DLL_PATH" ]]; then
    dll="$DLL_PATH"
    echo "Using explicit DLL: $dll"
elif [[ -n "$script_dir" && -f "$script_dir/../Release/exu.dll" ]]; then
    dll="$(cd "$script_dir/.." && pwd)/Release/exu.dll"
    echo "Using local Release build: $dll"
elif download_matched_release "$work/release"; then
    dll="$work/release/exu.dll"
else
    echo "error: could not download exu.dll from $REPO_SLUG." >&2
    echo "Pass --dll, or set EXU_REPO to a repo that publishes releases." >&2
    exit 1
fi

if [[ -z "$dll" || ! -f "$dll" ]]; then
    echo "error: Extra Utilities DLL is missing." >&2
    exit 1
fi

echo "Installing to:"
printf '  %s\n' "${BZR_GAME_PATHS[@]}"

for game_dir in "${BZR_GAME_PATHS[@]}"; do
    deploy_matched "$game_dir" "$dll"
    echo
done

cat <<'EOF'

Install complete.

exu.dll is a Win32 Lua module. Proton loads it as a Windows DLL; no
WINEDLLOVERRIDES entry is required (unlike OpenShim's winmm.dll proxy).
EOF
