param(
    [string]$Version = '0.5.2',
    [string]$ModVersion = '3.0.12',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$release = Join-Path $root 'release'
$manualStage = Join-Path $dist 'manual-stage'

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Stage
    if ($LASTEXITCODE) { throw 'Release build failed' }
}

$dll = Join-Path $release 'd2rloader/plugins/d2rl-item-database.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "Missing staged release file: $dll" }
& (Join-Path $PSScriptRoot 'verify_embedded_resources.ps1') -Dll $dll
if ($LASTEXITCODE) { throw 'Embedded resource verification failed' }

if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Path $manualStage | Out-Null

$manualPlugins = Join-Path $manualStage 'd2rloader/plugins'
$manualDocs = Join-Path $manualPlugins 'item-database/docs'
New-Item -ItemType Directory -Path $manualPlugins, $manualDocs | Out-Null
Copy-Item -LiteralPath $dll -Destination $manualPlugins

$noticeFiles = @(
    'INSTALL.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CREDITS.md',
    'AI_DISCLOSURE.md', 'PRIVACY.md', 'CHANGELOG.md', 'docs/PERMISSIONS.md'
)
foreach ($relative in $noticeFiles) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $manualDocs
}
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $manualDocs -Recurse

$installerDll = Join-Path $dist 'd2rl-item-database.dll'
Copy-Item -LiteralPath $dll -Destination $installerDll
$manualZip = Join-Path $dist "D2RR-Item-Database-v$Version.zip"
Compress-Archive -Path (Join-Path $manualStage '*') -DestinationPath $manualZip -CompressionLevel Optimal

$hashLines = foreach ($artifact in @($installerDll, $manualZip)) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($artifact))"
}
$hashLines |
    Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

Remove-Item -LiteralPath $manualStage -Recurse -Force
Write-Output "Created $manualZip"
Write-Output "Created $installerDll for D2RLoader Extension upload"
Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
