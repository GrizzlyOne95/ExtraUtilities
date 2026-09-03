#!/usr/bin/env bash
#
# One-time developer environment setup for Extra Utilities.
#
# Performs a sparse checkout of the Ogre 1.10.0 headers required to compile
# ExtraUtilities into third_party/ogre-1.10.0-bzr/_work. This mirrors
# setup-dev.ps1 and the Windows CI header checkout.
#
# After this script succeeds on Linux you can run the Python validation
# tools. The Win32 exu.dll itself is still built with MSVC on Windows
# (Release | x86). There is no MinGW native of Extra Utilities.
#
# Notes:
#   - The _work/ directory is gitignored.
#   - The pinned commit matches ExtraUtilities.vcxproj / setup-dev.ps1.
#   - Run this script again safely; it is idempotent once the headers exist.

set -euo pipefail

ogre_commit="f1f1937fd6cbad05a4b9170b9882da91f42f53a5"
ogre_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/third_party/ogre-1.10.0-bzr/_work"
header_sentinel="$ogre_dir/OgreMain/include/OgreException.h"

if [ -f "$header_sentinel" ]; then
    echo "Ogre headers already present at: $ogre_dir"
    echo "Setup complete."
    exit 0
fi

echo "Fetching Ogre 1.10.0 headers (sparse checkout)..."
echo "Pinned commit: $ogre_commit"
echo "Target: $ogre_dir"

if ! command -v git >/dev/null 2>&1; then
    echo "error: git was not found on PATH" >&2
    exit 1
fi

if [ ! -d "$ogre_dir" ]; then
    mkdir -p "$ogre_dir"
fi

if [ ! -d "$ogre_dir/.git" ]; then
    git -C "$ogre_dir" init
    git -C "$ogre_dir" remote add origin https://github.com/OGRECave/ogre.git
fi

git -C "$ogre_dir" sparse-checkout init --cone
git -C "$ogre_dir" sparse-checkout set OgreMain/include Components/Overlay/include

git -C "$ogre_dir" fetch --filter=blob:none --depth 1 origin "$ogre_commit"
git -C "$ogre_dir" checkout "$ogre_commit"

echo ""
echo "Done. Ogre headers are ready at: $ogre_dir"
echo "Win32 DLL builds still require Visual Studio 2022 (see README)."
