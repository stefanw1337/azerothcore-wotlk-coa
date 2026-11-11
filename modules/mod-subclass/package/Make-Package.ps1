<#
  Builds the subclass package on the build PC and zips it.
  -Build    the subclass build tree (source in src, build in build). Its worldserver has no Skinshifter in it.
  -Server   the repack the server tables, the display table and LiveMon.exe are taken from.
  -Client   the game folder the client patch, the geometry file and the SpellActivationOverlay addon come from.
  -Work     where CoA-Subclass and the zip are written; it also holds ogre-kit-*.zip (the male ogre builder, its
            Python packages and CascExtractor), which goes into files\ogre.
#>
param(
    [string]$Build = 'L:\coa-build',
    [string]$Server = 'L:\CoA Server-Ogre-edits',
    [string]$Client = 'L:\CoA Client',
    [string]$Work = $PSScriptRoot,
    [string]$DatabaseRelease = '0cd699377',
    [string[]]$LocalOnly = @('Creature\TwoHeadedOgre\TwoHeadedOgreMale', 'Creature\ChoGall\')
)
$ErrorActionPreference = 'Stop'
function Step($text) { Write-Host "`n=== $text ===" -ForegroundColor Cyan }
function Run($exe) { & $exe @args; if ($LASTEXITCODE) { throw "$exe $args failed" } }

$src = Join-Path $Build 'src'
$python = Join-Path $Server 'Runtime\python\python.exe'
$tools = Join-Path $src 'modules\mod-subclass\tools'

Step '[1/7] The source is the pushed subclass branch'
Run git -C $src fetch --quiet origin
$dirty = git -C $src status --porcelain --untracked-files=no
if ($dirty) { throw "$src has local changes; commit or undo them first." }
$head = (git -C $src rev-parse HEAD).Trim()
$pushed = (git -C $src rev-parse origin/stefanw1337-subclassing-test).Trim()
if ($head -ne $pushed) { throw "$src is not at origin/stefanw1337-subclassing-test. Run the update bat first." }
if (Test-Path (Join-Path $src 'modules\mod-skinshifter')) { throw "mod-skinshifter is in $src; it must not be." }
Write-Host ('  ' + (git -C $src log --oneline -1))

Step '[2/7] worldserver.exe of that source'
$exe = Get-ChildItem (Join-Path $Build 'build') -Recurse -Filter worldserver.exe |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $exe) { throw "No worldserver.exe under $Build\build." }
$sources = Get-ChildItem (Join-Path $src 'src'), (Join-Path $src 'modules') -Recurse -File -Include *.cpp, *.h |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($sources.LastWriteTime -gt $exe.LastWriteTime) {
    throw "$($exe.FullName) is older than $($sources.FullName). Build $Build again first."
}
Write-Host "  $($exe.FullName) ($($exe.LastWriteTime))"

Step '[3/7] Installer, configs, SQL, server tables and LiveMon'
$package = Join-Path $Work 'CoA-Subclass'
Run $python -B (Join-Path $tools 'mkpackage.py') $src (Join-Path $Server 'Data\dbc_races') $DatabaseRelease $Work `
    (Join-Path $Server 'LiveMon\LiveMon.exe') (Join-Path $Server 'Data\dbc_clientset\CreatureDisplayInfo.dbc')
$files = Join-Path $package 'files'
Copy-Item $exe.FullName (Join-Path $files 'worldserver.exe')

Step '[4/7] Client patch, without the files that stay local'
$clientFiles = New-Item -ItemType Directory -Force -Path (Join-Path $files 'client')
Run $python -B (Join-Path $tools 'mkreleasepatch.py') (Join-Path $Client 'Data\patch-ZZZZZY.MPQ') `
    (Join-Path $clientFiles 'patch-ZZZZZY.MPQ') @LocalOnly
$root = New-Item -ItemType Directory -Force -Path (Join-Path $files 'client_root')
Copy-Item (Join-Path $Client 'EsteriaAppearanceGeometry.bin') $root

Step '[5/7] Addons and the male ogre builder'
Copy-Item (Join-Path $Client 'Interface\AddOns\SpellActivationOverlay-for-CoA') (Join-Path $files 'addons') -Recurse
$kit = Get-ChildItem $Work -Filter 'ogre-kit-*.zip' | Sort-Object Name -Descending | Select-Object -First 1
if (-not $kit) { throw "ogre-kit-*.zip is missing in $Work." }
Expand-Archive -Path $kit.FullName -DestinationPath (Join-Path $files 'ogre') -Force
Write-Host "  $($kit.Name)"

Step '[6/7] Nothing personal or local in the package'
$patterns = '[A-Z]:\\(coa-build|CoA Client|CoA Server|WoW-Skinshifter|Users\\)', 'HotS', 'Skinshifter'
$leaks = Get-ChildItem $package -Recurse -File -Include *.txt, *.json, *.ini, *.dist, *.py, *.bat, *.lua, *.toc |
    Where-Object { $_.FullName -notlike '*SpellActivationOverlay*' -and $_.FullName -notlike '*\ogre\toolkit\*' } |
    Select-String -CaseSensitive -Pattern $patterns
if ($leaks) {
    $leaks | ForEach-Object { Write-Host "  $_" }
    throw 'The package names a local path, HotS or Skinshifter.'
}
Write-Host '  clean'

Step '[7/7] Zip'
$info = Get-Content (Join-Path $package 'package.json') -Raw | ConvertFrom-Json
$zip = Join-Path $Work ("CoA-Subclass-{0}-{1}.zip" -f $info.version, $info.built)
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $package -DestinationPath $zip -CompressionLevel Optimal
Write-Host ("Package ready: {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB)) -ForegroundColor Green
