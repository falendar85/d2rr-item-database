param(
    [string]$Version = '0.5.1',
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
$database = Join-Path $release 'd2rloader/plugins/item-database/database.json'
$guides = Join-Path $release 'd2rloader/plugins/item-database/guides.json'
foreach ($file in @($dll, $database, $guides)) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Missing staged release file: $file" }
}

# Fail before packaging if the data files are not valid JSON.
Get-Content -Raw -LiteralPath $database | ConvertFrom-Json | Out-Null
Get-Content -Raw -LiteralPath $guides | ConvertFrom-Json | Out-Null

if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Path $manualStage | Out-Null

$manualPlugins = Join-Path $manualStage 'd2rloader/plugins'
$manualData = Join-Path $manualPlugins 'item-database'
$manualDocs = Join-Path $manualData 'docs'
New-Item -ItemType Directory -Path $manualPlugins, $manualData, $manualDocs | Out-Null
Copy-Item -LiteralPath $dll -Destination $manualPlugins
Copy-Item -LiteralPath $database, $guides -Destination $manualData

$noticeFiles = @(
    'INSTALL.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CREDITS.md',
    'AI_DISCLOSURE.md', 'PRIVACY.md', 'CHANGELOG.md', 'docs/PERMISSIONS.md'
)
foreach ($relative in $noticeFiles) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $manualDocs
}
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $manualDocs -Recurse

# The launcher's ordinary Hub assets are confined to Reimagined.mpq. This native
# extension belongs in the sibling mods/Reimagined/d2rloader tree, so deliberately
# produce only the manual package until the Hub supports that destination.
$manualZip = Join-Path $dist "D2RR-Item-Database-v$Version.zip"
Compress-Archive -Path (Join-Path $manualStage '*') -DestinationPath $manualZip -CompressionLevel Optimal

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $manualZip).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($manualZip))" |
    Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

Remove-Item -LiteralPath $manualStage -Recurse -Force
Write-Output "Created $manualZip"
Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
