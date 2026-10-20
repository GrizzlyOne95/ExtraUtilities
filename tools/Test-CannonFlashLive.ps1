<# Reversible, windowed Windows/GOG DX11 artwork trial. No Lua flash producer.
   Requires OpenShim's shared harness and an installed lcbench map/resource group.
   Run in a fresh Windows PowerShell 5.1 process. #>
param(
 [Parameter(Mandatory=$true)][string]$OpenShimRepo,
 [string]$GameRoot='C:\Program Files (x86)\GOG Galaxy\Games\Battlezone 98 Redux',
 [ValidateSet('small','heavy')][string]$Size='small',
 [ValidateSet('Amber','Blue')][string]$Colour='Amber',
 [switch]$Smoke
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$shim=(Resolve-Path -LiteralPath $OpenShimRepo).Path
$game=(Resolve-Path -LiteralPath $GameRoot).Path
$out=Join-Path $repo ('build\flash-v2\run-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$inheritedWindowed=$env:BZR_FORCE_WINDOWED
try {$env:BZR_FORCE_WINDOWED=$null; . "$shim\reverse_engineering\BZRHarness.ps1"}
finally {$env:BZR_FORCE_WINDOWED=$inheritedWindowed}
. "$shim\reverse_engineering\BZRWindowInput.ps1"
$lock=Enter-BZRLaunchLock -TimeoutSeconds 2
$proc=$null; $ogre=$null; $saved=New-Object System.Collections.Generic.List[object]
function Save-File([string]$path) {
 if(@($saved | Where-Object {$_.path -eq $path}).Count){return}
 $backup=Join-Path $out ('backup-'+$saved.Count)
 $exists=Test-Path -LiteralPath $path -PathType Leaf
 $hash=''
 if($exists){Copy-Item -LiteralPath $path -Destination $backup; $hash=(Get-FileHash -LiteralPath $path).Hash}
 $saved.Add([pscustomobject]@{path=$path;backup=$backup;exists=$exists;hash=$hash})
 $saved | ConvertTo-Json | Set-Content "$out\restoration.json"
}
function Stage-File([string]$source,[string]$target) {
 Save-File $target
 Copy-Item -LiteralPath $source -Destination $target -Force
}
try {
 Assert-BZRSafeToLaunch
 $env:BZR_FORCE_WINDOWED='1'
 $ogre=Set-BZROgreWindowed -GameRoot $game -VideoMode '1280 x 720 @ 32-bit colour'
 foreach($name in @('winmm.dll','bzloader.dll','plugins\openshim.dll','scripts\patches.json','openshim\OpenShimAssets.ini','openshim.ini','career_stats.cfg')){Save-File "$game\$name"}
 foreach($item in Get-ChildItem "$shim\resources\renderer\enhanced" -File){Save-File "$game\openshim\renderer\enhanced\$($item.Name)"}
 foreach($item in Get-ChildItem "$shim\resources\ui\custom_widgets" -Filter '*.png' -File){Save-File "$game\BZ_ASSETS_CORE\common\ui\CustomWidgets\$($item.Name)"}
 & "$shim\scripts\Deploy-OpenShim.ps1" -GameDir $game
 $ini=[IO.File]::ReadAllText("$game\openshim.ini")
 foreach($setting in @(@('MuzzleFlash','1'),@('MeshRecoil','0'),@('TerrainHdEnabled','0'),@('TerrainMicroRelief','0'))){
  $pattern='(?m)^'+$setting[0]+'\s*=.*$'
  if($ini -notmatch $pattern){throw "Missing settings key $($setting[0])"}
  $ini=[regex]::Replace($ini,$pattern,($setting[0]+' = '+$setting[1]))
 }
 [IO.File]::WriteAllText("$game\openshim.ini",$ini)
 foreach($extension in @('bzn','trn','mat','hg2','lgt','ini')){
  Stage-File "$game\addon\lcbench\lcbench.$extension" "$game\addon\lcbench.$extension"
 }
 Stage-File "$game\StockODFFiles\avtank.odf" "$game\addon\avtank.odf"
 $options=@{GameRoot=$game;SourceOdf="$game\StockODFFiles\gatstab.odf";Mesh="exu_${Size}_flash.mesh";Material="EXU/CannonFlash/$Colour";Duration=0.12;Scale=1;EndScale=0.65;Offset=@(0.20,0.10,2.41)}
 if($Smoke){$options.MuzzleRender='oshmuzz.render';$options.MuzzleRenderDuration=0.12}
 $prepared=& "$shim\reverse_engineering\Prepare-OdfMuzzleFlashTrial.ps1" @options 6>&1 | Out-String
 if($prepared -notmatch 'Prepared (.+\.odf) \(game unchanged\)'){throw "Cannot find prepared ODF: $prepared"}
 Stage-File $Matches[1].Trim() "$game\addon\gatstab.odf"
 $resources="$game\addon\stock_terrain_hd"
 if(-not (Test-Path -LiteralPath $resources -PathType Container)){throw 'The installed test resource group stock_terrain_hd is required'}
 foreach($item in Get-ChildItem "$repo\examples\odf_cannon_flash" -File | Where-Object {$_.Extension -in @('.mesh','.hlsl','.program','.material','.png')}){
  Stage-File $item.FullName "$resources\$($item.Name)"
 }
 if($Smoke){Stage-File "$shim\examples\odf_muzzle_render\oshmuzz.odf" "$game\addon\oshmuzz.odf"}
 # Explicitly load this build before the native bridge discovers exu.dll.
 $dll=(Resolve-Path "$repo\Release\exu.dll").Path.Replace('\','/')
 $driver=[IO.File]::ReadAllText("$shim\reverse_engineering\test_missions\weapon_presentation\avtank_odf_trial.lua")
 $load="local loader,message=package.loadlib([[$dll]],'luaopen_exu'); assert(loader,message); loader(); exu=assert(rawget(_G,'exu'))"
 $driver=$driver.Replace('exu = require("exu")',$load)
 Save-File "$game\addon\lcbench.lua"
 [IO.File]::WriteAllText("$game\addon\lcbench.lua",$driver)
 Assert-BZRSafeToLaunch
 $proc=Start-BZRGameProcess -FilePath "$game\battlezone98redux.exe" -ArgumentList 'lcbench.bzn /nointro /renderer:dx11' -WorkingDirectory $game -PassThru
 [pscustomobject]@{pid=$proc.Id;size=$Size;colour=$Colour;smoke=[bool]$Smoke;dll=$dll;dllHash=(Get-FileHash "$repo\Release\exu.dll").Hash} | ConvertTo-Json | Set-Content "$out\trial.json"
 Write-Host "PID=$($proc.Id) SIZE=$Size COLOUR=$Colour OUT=$out"
 Start-Sleep -Seconds 20
 $window=[BZRWin.Native]::MainWindow($proc.Id)
 if($window -eq [IntPtr]::Zero){throw 'Game window unavailable'}
 [BZRWin.Native]::Key($window,0x20)
 Start-Sleep -Seconds 10
 foreach($frame in 0..119){
  [BZRWin.Native]::Capture($window,(Join-Path $out ('frame-{0:D3}.png' -f $frame)))
  Start-Sleep -Milliseconds 25
 }
 Write-Host 'CAPTURED'
} finally {
 if($proc){Stop-BZRGame -Id $proc.Id -TimeoutSeconds 15}
 foreach($name in @('openshim.log','exu.log','BZLogger.txt','BZOgreLogfile.log')){if(Test-Path "$game\logs\$name"){Copy-Item "$game\logs\$name" "$out\$name" -Force}}
 foreach($entry in $saved){
  if($entry.exists){Copy-Item -LiteralPath $entry.backup -Destination $entry.path -Force
   if((Get-FileHash -LiteralPath $entry.path).Hash -ne $entry.hash){throw "RESTORE HASH FAILURE $($entry.path)"}
  }elseif(Test-Path -LiteralPath $entry.path){Remove-Item -LiteralPath $entry.path}
 }
 if($null-ne$ogre){Restore-BZROgreConfig -GameRoot $game -Original $ogre}
 Exit-BZRLaunchLock $lock
 Write-Host "RESTORED $($saved.Count) files. Evidence: $out"
}
$flashLog=Get-Content "$out\exu.log"
if(-not ($flashLog -match 'visualState=1')){throw 'Per-flash visual state was not observed; inspect logs'}
if(-not ($flashLog -match 'shutdown: destroyed 0 live; totals spawned=[1-9][0-9]* recycled=0 expired=[0-9]+ failed=0 orphaned=0')){throw 'Clean flash shutdown was not observed; inspect logs'}
$renderLog=Get-Content "$out\BZOgreLogfile.log"
if(-not ($renderLog -match 'Texture: exu_muzzle_flame_v2.png: Loading')){throw 'Authored flame texture did not load'}
if($Smoke){
 $nativeLog=Get-Content "$out\openshim.log" | Where-Object {$_ -match "\[pid:$($proc.Id) "}
 if(-not ($nativeLog -match 'muzzle render created effect=oshmuzz.render') -or -not ($nativeLog -match 'muzzle render detached count=')){throw 'Native smoke/light creation and detachment were not observed'}
}
