param(
    [string]$Version = '0.5.0',
    [string]$ModVersion = '3.0.12',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$release = Join-Path $root 'release'
$manualStage = Join-Path $dist 'manual-stage'
$hubStage = Join-Path $dist 'hub-stage'

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
New-Item -ItemType Directory -Path $manualStage, $hubStage | Out-Null

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

$hubAssets = Join-Path $hubStage 'assets'
$hubData = Join-Path $hubAssets 'item-database'
$hubDocs = Join-Path $hubData 'docs'
New-Item -ItemType Directory -Path $hubAssets, $hubData, $hubDocs | Out-Null
Copy-Item -LiteralPath $dll -Destination $hubAssets
Copy-Item -LiteralPath $database, $guides -Destination $hubData
foreach ($relative in $noticeFiles) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $hubDocs
}
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $hubDocs -Recurse

$assets = @(
    @{ source = 'assets/d2rl-item-database.dll'; target = 'd2rloader/plugins/d2rl-item-database.dll' },
    @{ source = 'assets/item-database/database.json'; target = 'd2rloader/plugins/item-database/database.json' },
    @{ source = 'assets/item-database/guides.json'; target = 'd2rloader/plugins/item-database/guides.json' }
)
foreach ($file in Get-ChildItem -LiteralPath $hubDocs -File -Recurse) {
    $source = $file.FullName.Substring($hubStage.Length + 1).Replace('\', '/')
    $target = $source.Substring('assets/'.Length)
    $assets += @{ source = $source; target = "d2rloader/plugins/$target" }
}
$manifest = [ordered]@{
    name = 'D2RR Item Database'
    version = $Version
    modVersion = $ModVersion
    author = 'Falendar and contributors'
    description = 'Offline searchable item, recipe, crafting, enchant, and orb reference overlay. Open with Alt+S.'
    files = @()
    parameters = @()
    assets = $assets
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $hubStage 'plugininfo.json') -Encoding utf8NoBOM

$manualZip = Join-Path $dist "D2RR-Item-Database-v$Version.zip"
$hubZip = Join-Path $dist "D2RR-Item-Database-Hub-v$Version.zip"
Compress-Archive -Path (Join-Path $manualStage '*') -DestinationPath $manualZip -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $hubStage '*') -DestinationPath $hubZip -CompressionLevel Optimal

$hashLines = foreach ($archive in @($manualZip, $hubZip)) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($archive))"
}
$hashLines | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

Remove-Item -LiteralPath $manualStage, $hubStage -Recurse -Force
Write-Output "Created $manualZip"
Write-Output "Created $hubZip"
Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
