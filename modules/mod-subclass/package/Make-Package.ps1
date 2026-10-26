<#
  Completes the subclass test package on the build PC and zips it.
  Inputs in -Work: CoA-Subclass-source.zip and the patch-ZZZZS.MPQ.part* files from the source side.
  Adds the worldserver.exe of the local build, the SpellActivationOverlay addon and our client patches.
#>
param(
    [string]$Work = 'L:\CoA-Subclass-Package',
    [string]$Build = 'L:\coa-build',
    [string]$Client = 'L:\CoA Client'
)
$ErrorActionPreference = 'Stop'
function Step($text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }

$package = Join-Path $Work 'CoA-Subclass'
Step 'Source side'
if (Test-Path $package) { Remove-Item $package -Recurse -Force }
Expand-Archive -Path (Join-Path $Work 'CoA-Subclass-source.zip') -DestinationPath $Work -Force
$files = Join-Path $package 'files'
$info = Get-Content (Join-Path $package 'package.json') -Raw | ConvertFrom-Json

Step 'worldserver.exe from the local build'
$exe = Get-ChildItem (Join-Path $Build 'build') -Recurse -Filter worldserver.exe | Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $exe) { throw "No worldserver.exe under $Build\build: run Build local build.bat first." }
$built = (git -C (Join-Path $Build 'src') log -1 --format=%h).Trim()
if (-not $info.core.StartsWith($built) -and -not $built.StartsWith($info.core)) {
    throw "The local build is at $built but the package was assembled at $($info.core). Build again first."
}
Copy-Item $exe.FullName (Join-Path $files 'worldserver.exe')

Step 'Client patches and addons'
$clientFiles = New-Item -ItemType Directory -Force -Path (Join-Path $files 'client')
$parts = Get-ChildItem $Work -Filter 'patch-ZZZZS.MPQ.part*' | Sort-Object Name
if (-not $parts) { throw "patch-ZZZZS.MPQ.part* are missing in $Work." }
$target = [IO.File]::Create((Join-Path $clientFiles 'patch-ZZZZS.MPQ'))
foreach ($part in $parts) { $bytes = [IO.File]::ReadAllBytes($part.FullName); $target.Write($bytes, 0, $bytes.Length) }
$target.Close()
foreach ($name in 'patch-ZZZZC.MPQ', 'patch-ZZZZW.MPQ') { Copy-Item (Join-Path $Client "Data\$name") $clientFiles }
$sao = Join-Path $Client 'Interface\AddOns\SpellActivationOverlay-for-CoA'
Copy-Item $sao (Join-Path $files 'addons') -Recurse -Force

Step 'Zip'
$zip = Join-Path $Work ("CoA-Subclass-{0}-{1}.zip" -f $info.built, $info.core)
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $package -DestinationPath $zip -CompressionLevel Optimal
Write-Host ("Package ready: {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)) -ForegroundColor Green
