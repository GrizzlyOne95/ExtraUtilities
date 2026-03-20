param(
    [Parameter(Mandatory = $true)]
    [string]$GameDir,

    [Parameter(Mandatory = $true)]
    [string]$RebuiltOgreMain,

    [string]$DumpbinPath
)

$ErrorActionPreference = "Stop"

function Normalize-SymbolName {
    param([string]$Symbol)

    if (-not $Symbol) {
        return $Symbol
    }

    return (($Symbol -replace '\s+=\s+@ILT\+\d+\(.+\)$', '').Trim())
}

function Find-Dumpbin {
    param([string]$ExplicitPath)

    if ($ExplicitPath) {
        if (-not (Test-Path $ExplicitPath)) {
            throw "dumpbin.exe not found at '$ExplicitPath'."
        }
        return (Resolve-Path $ExplicitPath).Path
    }

    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86\dumpbin.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130\bin\Hostx64\x86\dumpbin.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $discovered = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like "*\\Hostx64\\x86\\dumpbin.exe" } |
        Select-Object -First 1 -ExpandProperty FullName

    if ($discovered) {
        return $discovered
    }

    throw "Unable to locate dumpbin.exe. Pass -DumpbinPath explicitly."
}

function Get-Exports {
    param(
        [string]$Dumpbin,
        [string]$Path
    )

    $text = & $Dumpbin /nologo /exports $Path | Out-String
    [regex]::Matches($text, '(?m)^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(.+?)\s*$') |
        ForEach-Object { Normalize-SymbolName $_.Groups[1].Value }
}

function Get-ImportsFrom {
    param(
        [string]$Dumpbin,
        [string]$Path,
        [string]$DllName
    )

    $text = & $Dumpbin /nologo /imports $Path | Out-String
    $pattern = '(?ms)\r?\n\s*' + [regex]::Escape($DllName) + '\r?\n(.*?)(?=\r?\n\s*\S+\.dll\r?\n|\z)'
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) {
        return @()
    }

    [regex]::Matches($match.Groups[1].Value, '(?m)^\s+[0-9A-F]+\s+([?@A-Za-z_].+?)\s*$') |
        ForEach-Object { Normalize-SymbolName $_.Groups[1].Value } |
        Where-Object {
            $_ -and $_ -notmatch '^(Import Address Table|Import Name Table|time date stamp|Index of first forwarder reference)$'
        }
}

$dumpbin = Find-Dumpbin -ExplicitPath $DumpbinPath
$gameDir = (Resolve-Path $GameDir).Path
$rebuiltOgreMain = (Resolve-Path $RebuiltOgreMain).Path
$shippedOgreMain = Join-Path $gameDir "OgreMain.dll"

if (-not (Test-Path $shippedOgreMain)) {
    throw "Shipped OgreMain.dll not found under '$gameDir'."
}

$shippedExports = Get-Exports -Dumpbin $dumpbin -Path $shippedOgreMain
$rebuiltExports = Get-Exports -Dumpbin $dumpbin -Path $rebuiltOgreMain

$onlyInRebuilt = @($rebuiltExports | Where-Object { $_ -notin $shippedExports })
$onlyInShipped = @($shippedExports | Where-Object { $_ -notin $rebuiltExports })

Write-Host "Shipped OgreMain exports: $($shippedExports.Count)"
Write-Host "Rebuilt OgreMain exports: $($rebuiltExports.Count)"
Write-Host "Only in rebuilt: $($onlyInRebuilt.Count)"
Write-Host "Only in shipped: $($onlyInShipped.Count)"
Write-Host ""

$copyCtor = "??0GpuProgram@Ogre@@QAE@ABV01@@Z"
Write-Host "GpuProgram copy ctor exported by shipped OgreMain: $($shippedExports -contains $copyCtor)"
Write-Host "GpuProgram copy ctor exported by rebuilt OgreMain: $($rebuiltExports -contains $copyCtor)"
Write-Host ""

$importers = Get-ChildItem $gameDir -File | Where-Object { $_.Extension -in ".exe", ".dll" }
$report = foreach ($file in $importers) {
    $imports = Get-ImportsFrom -Dumpbin $dumpbin -Path $file.FullName -DllName "OgreMain.dll"
    if ($imports.Count -eq 0) {
        continue
    }

    $missing = @($imports | Where-Object { $_ -notin $rebuiltExports })
    [pscustomobject]@{
        File = $file.Name
        OgreMainImports = $imports.Count
        MissingFromRebuilt = $missing.Count
        MissingExamples = ($missing | Select-Object -First 6) -join "; "
    }
}

$report |
    Sort-Object -Property @{ Expression = "MissingFromRebuilt"; Descending = $true }, @{ Expression = "File"; Descending = $false } |
    Format-Table -AutoSize

Write-Host ""
Write-Host "First 20 symbols only in rebuilt OgreMain:"
$onlyInRebuilt | Select-Object -First 20

Write-Host ""
Write-Host "First 20 symbols only in shipped OgreMain:"
$onlyInShipped | Select-Object -First 20
