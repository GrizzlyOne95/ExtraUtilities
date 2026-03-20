param(
    [Parameter(Mandatory = $true)]
    [string]$OgreSourceDir,
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Toolset = "v120",
    [ValidateSet("D3D11", "D3D9")]
    [string]$RenderSystem = "D3D11",
    [string]$WorkRoot = (Join-Path $PSScriptRoot "_work"),
    [switch]$BattlezoneAbiProfile,
    [switch]$BuildRelease,
    [switch]$SkipPatch,
    [switch]$DisableFreeImage,
    [string]$FreeImageRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Resolve-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $resolved = @($Candidates | Where-Object { $_ -and (Test-Path $_) })
    if ($resolved.Count -eq 0) {
        throw "Unable to find $Name."
    }

    return $resolved[0]
}

function Resolve-CMake {
    $candidates = @(
        (Get-Command cmake.exe -ErrorAction SilentlyContinue | ForEach-Object Source),
        "$env:APPDATA\Python\Python312\Scripts\cmake.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts\cmake.exe"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    return Resolve-Tool -Name "cmake.exe" -Candidates $candidates
}

function Resolve-Git {
    $candidates = @(
        (Get-Command git.exe -ErrorAction SilentlyContinue | ForEach-Object Source),
        "$env:ProgramFiles\Git\bin\git.exe",
        "${env:ProgramFiles(x86)}\Git\bin\git.exe"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    return Resolve-Tool -Name "git.exe" -Candidates $candidates
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Exe,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Exe $($Arguments -join ' ')"
    }
}

function Ensure-DownloadedArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Url,
        [Parameter(Mandatory = $true)]
        [string]$ArchivePath
    )

    if ((Test-Path $ArchivePath) -and (Get-Item $ArchivePath).Length -gt 0) {
        return
    }

    & curl.exe -L --fail $Url -o $ArchivePath
    if ($LASTEXITCODE -ne 0) {
        throw "Download failed: $Url"
    }
}

function Ensure-ExtractedArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ArchivePath,
        [Parameter(Mandatory = $true)]
        [string]$ExtractedDir
    )

    if (Test-Path $ExtractedDir) {
        return
    }

    Expand-Archive -Path $ArchivePath -DestinationPath (Split-Path $ExtractedDir -Parent) -Force
}

function Resolve-FirstExistingPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    throw "$Description not found."
}

function Ensure-PatchApplied {
    param(
        [Parameter(Mandatory = $true)]
        [string]$GitExe,
        [Parameter(Mandatory = $true)]
        [string]$PatchFile,
        [Parameter(Mandatory = $true)]
        [string]$SourceDir
    )

    Push-Location $SourceDir
    try {
        & $GitExe apply --reverse --check $PatchFile *> $null
        if ($LASTEXITCODE -eq 0) {
            return
        }

        & $GitExe apply --check $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch cannot be applied cleanly: $PatchFile"
        }

        & $GitExe apply $PatchFile
        if ($LASTEXITCODE -ne 0) {
            throw "Patch apply failed: $PatchFile"
        }
    }
    finally {
        Pop-Location
    }
}

function Ensure-GitClone {
    param(
        [Parameter(Mandatory = $true)]
        [string]$GitExe,
        [Parameter(Mandatory = $true)]
        [string]$Url,
        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (Test-Path (Join-Path $Destination ".git")) {
        return
    }

    if (Test-Path $Destination) {
        Remove-Item -Recurse -Force $Destination
    }

    & $GitExe clone --depth 1 $Url $Destination
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed: $Url"
    }
}

function Ensure-OgreFreeImageImportMode {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDir
    )

    $cmakeLists = Join-Path $SourceDir "OgreMain\CMakeLists.txt"
    if (-not (Test-Path $cmakeLists)) {
        throw "OgreMain CMakeLists not found: $cmakeLists"
    }

    $content = Get-Content -Path $cmakeLists -Raw
    $updated = $content -replace "(?m)^  add_definitions\(-DFREEIMAGE_LIB\)\r?\n", ""
    if ($updated -ne $content) {
        Set-Content -Path $cmakeLists -Value $updated -NoNewline
    }
}

$cmakeExe = Resolve-CMake
$gitExe = Resolve-Git

$toolchainTag = if ($Toolset) { $Toolset } else { "default" }
$renderTag = $RenderSystem.ToLower()
$profileTag = if ($BattlezoneAbiProfile) { "bzabi" } else { "default" }
$thirdPartyRoot = Join-Path $WorkRoot "thirdparty"
$depsDir = Join-Path $WorkRoot "deps-$toolchainTag-x86-$profileTag"
$buildDir = Join-Path $WorkRoot "build-$toolchainTag-$renderTag-$profileTag"

New-Item -ItemType Directory -Force -Path $WorkRoot, $thirdPartyRoot, $depsDir | Out-Null

if (-not (Test-Path $OgreSourceDir)) {
    throw "Ogre source directory not found: $OgreSourceDir"
}

if (-not $SkipPatch) {
    $patchFile = Join-Path $PSScriptRoot "patches\0001-suppress-cross-renderer-hlsl-target-warnings.patch"
    Ensure-PatchApplied -GitExe $gitExe -PatchFile $patchFile -SourceDir $OgreSourceDir
}

if ($RenderSystem -eq "D3D9" -and -not $env:DXSDK_DIR) {
    $defaultDxSdk = "C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)"
    if (Test-Path $defaultDxSdk) {
        $env:DXSDK_DIR = $defaultDxSdk
    }
    else {
        throw "D3D9 builds require DXSDK_DIR or the June 2010 DirectX SDK in the default install path."
    }
}

$zlibArchive = Join-Path $thirdPartyRoot "zlib-1.3.1.zip"
$zlibSource = Join-Path $thirdPartyRoot "zlib-1.3.1"
$zlibBuild = Join-Path $thirdPartyRoot "zlib-1.3.1-build-$toolchainTag-x86"

$freetypeArchive = Join-Path $thirdPartyRoot "freetype-2.13.3.zip"
$freetypeSource = Join-Path $thirdPartyRoot "freetype-VER-2-13-3"
$freetypeBuild = Join-Path $thirdPartyRoot "freetype-2.13.3-build-$toolchainTag-x86"

$zzipArchive = Join-Path $thirdPartyRoot "zziplib-master.zip"
$zzipSource = Join-Path $thirdPartyRoot "zziplib-master"
$zzipBuild = Join-Path $thirdPartyRoot "zziplib-master-build-$toolchainTag-x86"
$freeImageArchive = Join-Path $thirdPartyRoot "FreeImage3180Win32Win64.zip"
$freeImageArchiveRoot = Join-Path $thirdPartyRoot "freeimage-official-bin"
$defaultFreeImageRoot = Join-Path $freeImageArchiveRoot "FreeImage"

Ensure-DownloadedArchive -Url "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.zip" -ArchivePath $zlibArchive
Ensure-ExtractedArchive -ArchivePath $zlibArchive -ExtractedDir $zlibSource

Ensure-DownloadedArchive -Url "https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.zip" -ArchivePath $freetypeArchive
Ensure-ExtractedArchive -ArchivePath $freetypeArchive -ExtractedDir $freetypeSource

if ($BattlezoneAbiProfile) {
    Ensure-DownloadedArchive -Url "https://github.com/gdraheim/zziplib/archive/refs/heads/master.zip" -ArchivePath $zzipArchive
    Ensure-ExtractedArchive -ArchivePath $zzipArchive -ExtractedDir $zzipSource
    Ensure-DownloadedArchive -Url "https://downloads.sourceforge.net/freeimage/FreeImage3180Win32Win64.zip" -ArchivePath $freeImageArchive
    Ensure-ExtractedArchive -ArchivePath $freeImageArchive -ExtractedDir $defaultFreeImageRoot
}

$zlibArgs = @(
    "-S", $zlibSource,
    "-B", $zlibBuild,
    "-G", $Generator,
    "-A", "Win32",
    "-DCMAKE_INSTALL_PREFIX=$depsDir"
)
if ($Toolset) {
    $zlibArgs += @("-T", $Toolset)
}
Invoke-Checked -Exe $cmakeExe -Arguments $zlibArgs
Invoke-Checked -Exe $cmakeExe -Arguments @("--build", $zlibBuild, "--config", "Release", "--target", "install")

$freetypeArgs = @(
    "-S", $freetypeSource,
    "-B", $freetypeBuild,
    "-G", $Generator,
    "-A", "Win32",
    "-DCMAKE_INSTALL_PREFIX=$depsDir",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DFT_DISABLE_ZLIB=TRUE",
    "-DFT_DISABLE_BZIP2=TRUE",
    "-DFT_DISABLE_PNG=TRUE",
    "-DFT_DISABLE_HARFBUZZ=TRUE",
    "-DFT_DISABLE_BROTLI=TRUE"
)
if ($Toolset) {
    $freetypeArgs += @("-T", $Toolset)
}
Invoke-Checked -Exe $cmakeExe -Arguments $freetypeArgs
Invoke-Checked -Exe $cmakeExe -Arguments @("--build", $freetypeBuild, "--config", "Release", "--target", "install")

if ($BattlezoneAbiProfile) {
    $zzipArgs = @(
        "-S", $zzipSource,
        "-B", $zzipBuild,
        "-G", $Generator,
        "-A", "Win32",
        "-DCMAKE_INSTALL_PREFIX=$depsDir",
        "-DZLIB_ROOT=$depsDir",
        "-DBUILD_SHARED_LIBS=OFF",
        "-DMSVC_STATIC_RUNTIME=OFF",
        "-DZZIPDOCS=OFF",
        "-DZZIPBINS=OFF",
        "-DZZIPTEST=OFF",
        "-DZZIPSDL=OFF",
        "-DZZIPWRAP=OFF",
        "-DZZIPMMAPPED=OFF",
        "-DZZIPFSEEKO=OFF"
    )
    if ($Toolset) {
        $zzipArgs += @("-T", $Toolset)
    }
    Invoke-Checked -Exe $cmakeExe -Arguments $zzipArgs
    Invoke-Checked -Exe $cmakeExe -Arguments @("--build", $zzipBuild, "--config", "Release", "--target", "install")

    $zzipInstalledLib = Join-Path $depsDir "lib\\zzip-0.lib"
    if (Test-Path $zzipInstalledLib) {
        Copy-Item $zzipInstalledLib (Join-Path $depsDir "lib\\zziplib.lib") -Force
        Copy-Item $zzipInstalledLib (Join-Path $depsDir "lib\\zzip.lib") -Force
    }
}

$buildD3D11 = if ($RenderSystem -eq "D3D11") { "TRUE" } else { "FALSE" }
$buildD3D9 = if ($RenderSystem -eq "D3D9") { "TRUE" } else { "FALSE" }
$useCustomAllocators = if ($BattlezoneAbiProfile) { "TRUE" } else { "FALSE" }
$useCustomStringAllocator = "FALSE"
$enableZip = if ($BattlezoneAbiProfile) { "TRUE" } else { "FALSE" }
$enableStbi = if ($BattlezoneAbiProfile) { "FALSE" } else { "TRUE" }
$enableFreeImage = "FALSE"

if ($BattlezoneAbiProfile -and -not $DisableFreeImage -and -not $FreeImageRoot) {
    $FreeImageRoot = $defaultFreeImageRoot
}

if (-not $DisableFreeImage -and $FreeImageRoot) {
    if (-not (Test-Path $FreeImageRoot)) {
        throw "FreeImage root not found: $FreeImageRoot"
    }

    $resolvedFreeImageRoot = (Resolve-Path $FreeImageRoot).Path
    $freeImageIncludeDir = Resolve-FirstExistingPath -Description "FreeImage.h" -Candidates @(
        (Join-Path $resolvedFreeImageRoot "Dist\\x32\\FreeImage.h"),
        (Join-Path $resolvedFreeImageRoot "Dist\\FreeImage.h"),
        (Join-Path $resolvedFreeImageRoot "include\\FreeImage.h"),
        (Join-Path $resolvedFreeImageRoot "Source\\FreeImage.h")
    )
    $freeImageIncludeDir = Split-Path $freeImageIncludeDir -Parent

    $freeImageLib = Resolve-FirstExistingPath -Description "FreeImage.lib" -Candidates @(
        (Join-Path $resolvedFreeImageRoot "Dist\\x32\\FreeImage.lib"),
        (Join-Path $resolvedFreeImageRoot "Release\\FreeImage.lib"),
        (Join-Path $resolvedFreeImageRoot "lib\\Win32\\FreeImage.lib"),
        (Join-Path $resolvedFreeImageRoot "lib\\FreeImage.lib")
    )

    New-Item -ItemType Directory -Force -Path (Join-Path $depsDir "include"), (Join-Path $depsDir "lib") | Out-Null
    Copy-Item (Join-Path $freeImageIncludeDir "FreeImage.h") (Join-Path $depsDir "include\\FreeImage.h") -Force
    Copy-Item $freeImageLib (Join-Path $depsDir "lib\\FreeImage.lib") -Force

    $freeImageIncludeDir = Join-Path $depsDir "include"
    $freeImageLib = Join-Path $depsDir "lib\\FreeImage.lib"
    $env:FREEIMAGE_HOME = $resolvedFreeImageRoot
    $enableFreeImage = "TRUE"
    $enableStbi = "FALSE"
    Ensure-OgreFreeImageImportMode -SourceDir $OgreSourceDir
}

$ogreArgs = @(
    "-S", $OgreSourceDir,
    "-B", $buildDir,
    "-G", $Generator,
    "-A", "Win32",
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
    "-DOGRE_DEPENDENCIES_DIR=$depsDir",
    "-DOGRE_BUILD_DEPENDENCIES=FALSE",
    "-DOGRE_BUILD_RENDERSYSTEM_D3D11=$buildD3D11",
    "-DOGRE_BUILD_RENDERSYSTEM_D3D9=$buildD3D9",
    "-DOGRE_BUILD_RENDERSYSTEM_GL=FALSE",
    "-DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=FALSE",
    "-DOGRE_BUILD_RENDERSYSTEM_GLES=FALSE",
    "-DOGRE_BUILD_RENDERSYSTEM_GLES2=FALSE",
    "-DOGRE_USE_BOOST=FALSE",
    "-DOGRE_CONFIG_THREADS=0",
    "-DOGRE_CONFIG_CONTAINERS_USE_CUSTOM_ALLOCATOR=$useCustomAllocators",
    "-DOGRE_CONFIG_STRING_USE_CUSTOM_ALLOCATOR=$useCustomStringAllocator",
    "-DOGRE_CONFIG_ENABLE_FREEIMAGE=$enableFreeImage",
    "-DOGRE_CONFIG_ENABLE_STBI=$enableStbi",
    "-DOGRE_CONFIG_ENABLE_ZIP=$enableZip",
    "-DOGRE_BUILD_SAMPLES=FALSE",
    "-DOGRE_BUILD_TESTS=FALSE",
    "-DOGRE_BUILD_TOOLS=FALSE"
)
if ($Toolset) {
    $ogreArgs += @("-T", $Toolset)
}
if ($FreeImageRoot) {
    $ogreArgs += @(
        "-DFreeImage_INCLUDE_DIR=$freeImageIncludeDir",
        "-DFreeImage_LIBRARY_REL=$freeImageLib",
        "-DFreeImage_LIBRARY_DBG=$freeImageLib"
    )
}
if ($BattlezoneAbiProfile) {
    $zzipIncludeDir = Join-Path $depsDir "include"
    $zzipLib = Join-Path $depsDir "lib\\zziplib.lib"
    if ((Test-Path (Join-Path $zzipIncludeDir "zzip\\zzip.h")) -and (Test-Path $zzipLib)) {
        $ogreArgs += @(
            "-DZZip_INCLUDE_DIR=$zzipIncludeDir",
            "-DZZip_LIBRARY_REL=$zzipLib",
            "-DZZip_LIBRARY_DBG=$zzipLib"
        )
    }
}
Invoke-Checked -Exe $cmakeExe -Arguments $ogreArgs

if ($BuildRelease) {
    $target = if ($RenderSystem -eq "D3D9") { "RenderSystem_Direct3D9" } else { "RenderSystem_Direct3D11" }
    Invoke-Checked -Exe $cmakeExe -Arguments @("--build", $buildDir, "--config", "Release", "--target", $target)
}

Write-Host ""
Write-Host "Patch package root: $PSScriptRoot"
Write-Host "Ogre source:        $OgreSourceDir"
Write-Host "Profile:            $profileTag"
Write-Host "Dependencies:       $depsDir"
Write-Host "Build:              $buildDir"
Write-Host "Release DLLs:       $(Join-Path $buildDir 'bin\Release')"
