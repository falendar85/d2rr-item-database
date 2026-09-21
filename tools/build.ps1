param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$CoreOnly, [switch]$Stage)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# Explicit environment avoids the Visual Studio generator's per-user SDK discovery.
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio 2022 Desktop development with C++, CMake and Windows SDK.' }
$vc=(Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$kits=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk=(Get-ChildItem "$kits\Include" -Directory | Where-Object { Test-Path "$kits\Lib\$($_.Name)\um\x64" } | Sort-Object Name -Descending | Select-Object -First 1).Name
$cmake=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja=Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH="$vc\bin\Hostx64\x64;$kits\bin\$sdk\x64;$ninja;$(Split-Path $cmake);$env:PATH"
$env:INCLUDE="$vc\include;$kits\Include\$sdk\ucrt;$kits\Include\$sdk\shared;$kits\Include\$sdk\um;$kits\Include\$sdk\winrt"
$env:LIB="$vc\lib\x64;$kits\Lib\$sdk\ucrt\x64;$kits\Lib\$sdk\um\x64"
$suffix=if($CoreOnly){'-core'}else{''}
$build=Join-Path $root "build-$Configuration$suffix"
$plugin=if($CoreOnly){'OFF'}else{'ON'}
& $cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DITEMDB_PLUGIN=$plugin"
if($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build $build
if($LASTEXITCODE) { throw 'Build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $build --output-on-failure
if($LASTEXITCODE) { throw 'Tests failed' }
if($Stage){
    if($CoreOnly){throw 'Cannot stage a core-only build'}
    & $cmake --install $build --prefix (Join-Path $root 'release')
    if($LASTEXITCODE) { throw 'Staging failed' }
}
