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

$ogreCommit = "f1f1937fd6cbad05a4b9170b9882da91f42f53a5"
$ogreDir    = Join-Path $PSScriptRoot "third_party\ogre-1.10.0-bzr\_work"

# Skip if already set up (idempotent)
if (Test-Path (Join-Path $ogreDir ".git")) {
    Write-Host "Ogre headers already present at: $ogreDir"
    Write-Host "Setup complete."
    exit 0
}

Write-Host "Fetching Ogre 1.10.0 headers (sparse checkout)..."
Write-Host "Target: $ogreDir"

git clone --filter=blob:none --no-checkout https://github.com/OGRECave/ogre.git $ogreDir
if ($LASTEXITCODE -ne 0) { throw "git clone failed" }

git -C $ogreDir sparse-checkout init --cone
if ($LASTEXITCODE -ne 0) { throw "sparse-checkout init failed" }

git -C $ogreDir sparse-checkout set OgreMain/include Components/Overlay/include
if ($LASTEXITCODE -ne 0) { throw "sparse-checkout set failed" }

git -C $ogreDir checkout $ogreCommit
if ($LASTEXITCODE -ne 0) { throw "git checkout failed" }

Write-Host ""
Write-Host "Done. Ogre headers are ready at: $ogreDir"
Write-Host "Open ExtraUtilities.sln and build."
