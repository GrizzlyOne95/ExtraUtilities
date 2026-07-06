<#
.SYNOPSIS
    One-time developer environment setup for Extra Utilities G1.

.DESCRIPTION
    Performs a sparse checkout of the Ogre 1.10.0 headers required to build
    the project into third_party\ogre-1.10.0-bzr\_work. This mirrors exactly
    what the CI workflow does, eliminating the need for a sibling directory or
    a full Ogre source clone just to compile.

    After running this script, open ExtraUtilities.sln in Visual Studio and
    build. No other setup is required.

.NOTES
    - The _work/ directory is gitignored inside third_party/ogre-1.10.0-bzr/.
    - If you already have a full Ogre 1.10.0 source clone somewhere, you can
      instead create a symlink or junction:
          mklink /J third_party\ogre-1.10.0-bzr\_work <path-to-ogre-source>
    - To BUILD Ogre from source (required only for renderer changes, not for
      compiling EXU), run third_party\ogre-1.10.0-bzr\Build-Ogre-BZR.ps1.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ogreCommit     = "f1f1937fd6cbad05a4b9170b9882da91f42f53a5"
$ogreDir        = Join-Path $PSScriptRoot "third_party\ogre-1.10.0-bzr\_work"
$headerSentinel = Join-Path $ogreDir "OgreMain\include\OgreException.h"

# Skip if the headers are already in place (idempotent, regardless of how they
# got there: a prior sparse checkout, or a junction to a full Ogre clone).
if (Test-Path $headerSentinel) {
    Write-Host "Ogre headers already present at: $ogreDir"
    Write-Host "Setup complete."
    exit 0
}

Write-Host "Fetching Ogre 1.10.0 headers (sparse checkout)..."
Write-Host "Target: $ogreDir"

# Use an in-place `git init` + sparse fetch rather than `git clone`. The _work
# directory may already exist with unrelated content (e.g. leftover
# Build-Ogre-BZR.ps1 output under build-*/, deps-*/, thirdparty/), and
# `git clone` refuses a non-empty target -- which previously left setup stuck
# with no way forward short of manually deleting _work. init + fetch works
# whether _work is missing, empty, or non-empty, because the sparse paths
# (OgreMain/include, Components/Overlay/include) don't collide with that
# leftover content.
if (-not (Test-Path $ogreDir)) {
    New-Item -ItemType Directory -Path $ogreDir | Out-Null
}

if (-not (Test-Path (Join-Path $ogreDir ".git"))) {
    git -C $ogreDir init
    if ($LASTEXITCODE -ne 0) { throw "git init failed" }

    git -C $ogreDir remote add origin https://github.com/OGRECave/ogre.git
    if ($LASTEXITCODE -ne 0) { throw "git remote add failed" }
}

git -C $ogreDir sparse-checkout init --cone
if ($LASTEXITCODE -ne 0) { throw "sparse-checkout init failed" }

git -C $ogreDir sparse-checkout set OgreMain/include Components/Overlay/include
if ($LASTEXITCODE -ne 0) { throw "sparse-checkout set failed" }

git -C $ogreDir fetch --filter=blob:none --depth 1 origin $ogreCommit
if ($LASTEXITCODE -ne 0) { throw "git fetch failed" }

git -C $ogreDir checkout $ogreCommit
if ($LASTEXITCODE -ne 0) { throw "git checkout failed" }

Write-Host ""
Write-Host "Done. Ogre headers are ready at: $ogreDir"
Write-Host "Open ExtraUtilities.sln and build."
