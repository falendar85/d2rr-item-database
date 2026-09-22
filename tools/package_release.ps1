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

$manualZip = Join-Path $dist "D2RR-Item-Database-v$Version.zip"
Compress-Archive -Path (Join-Path $manualStage '*') -DestinationPath $manualZip -CompressionLevel Optimal

$hubAssets = Join-Path $hubStage 'assets'
$hubData = Join-Path $hubAssets 'item-database'
$hubDocs = Join-Path $hubStage 'docs'
New-Item -ItemType Directory -Path $hubAssets, $hubData, $hubDocs | Out-Null
Copy-Item -LiteralPath $dll -Destination $hubAssets
Copy-Item -LiteralPath $database, $guides -Destination $hubData
foreach ($relative in $noticeFiles) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $hubDocs
}
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $hubDocs -Recurse

$manifest = [ordered]@{
    name = 'Item Database'
    version = $Version
    modVersion = $ModVersion
    author = 'Falendar and D2RR Item Database contributors'
    description = 'Offline searchable D2R Reimagined reference overlay. Press Alt+S to open or close it.'
    files = @()
    assets = @(
        [ordered]@{
            source = 'assets/d2rl-item-database.dll'
            target = 'plugins/d2rl-item-database.dll'
            targetRoot = 'd2rloader'
        },
        [ordered]@{
            source = 'assets/item-database/database.json'
            target = 'plugins/item-database/database.json'
            targetRoot = 'd2rloader'
        },
        [ordered]@{
            source = 'assets/item-database/guides.json'
            target = 'plugins/item-database/guides.json'
            targetRoot = 'd2rloader'
        }
    )
}
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $hubStage 'plugininfo.json') -Encoding utf8

$hubZip = Join-Path $dist "D2RR-Item-Database-Hub-v$Version.zip"
Compress-Archive -Path (Join-Path $hubStage '*') -DestinationPath $hubZip -CompressionLevel Optimal

@($manualZip, $hubZip) | ForEach-Object {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($_))"
} |
    Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

Remove-Item -LiteralPath $manualStage, $hubStage -Recurse -Force
Write-Output "Created $manualZip"
Write-Output "Created $hubZip"
Get-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
