# Builds a portable Smalto for Windows (x64) and zips it.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 [-Qt C:\Qt\6.10.3\msvc2022_64]
#
# Output: dist\Smalto-<version>-windows-x64.zip, which unpacks to a Smalto folder
# with Smalto.exe, the Qt libraries and plugins it needs, LICENSE and README.md.
# It needs Visual Studio (or its Build Tools) with the C++ workload, CMake and Ninja;
# the MSVC environment is found with vswhere, so any console works. Unsigned.
param(
    [string]$Qt = $(if ($env:QT_PREFIX) { $env:QT_PREFIX } else { 'C:\Qt\6.10.3\msvc2022_64' })
)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

$build = 'build\package'
$dist = 'dist'
$version = (Select-String -Path CMakeLists.txt -Pattern '^project\(Smalto VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = '0.0.0' }
if (-not (Test-Path "$Qt\bin\windeployqt.exe")) { throw "No Qt at $Qt (pass -Qt <prefix>)" }
Write-Host "Qt: $Qt, Smalto $version"

# Take over the environment of vcvars64.bat, unless this already is an MSVC console.
# (cl.exe on PATH isn't enough: INCLUDE, LIB and VS's Ninja come with vcvars.)
if (-not $env:VSCMD_VER) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: is Visual Studio installed?' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio with the C++ x64 tools found.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    # `call` first: cmd strips the outer quotes of a command line that starts with one.
    cmd /c "call `"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
}

function Invoke-Checked {
    & $args[0] $args[1..($args.Count - 1)]
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed ($LASTEXITCODE)" }
}

Invoke-Checked cmake -S . -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$Qt" -DSMALTO_BUILD_TESTS=OFF
Invoke-Checked cmake --build $build

$stage = Join-Path $dist 'Smalto'
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item "$build\Smalto.exe" $stage
# No compiler runtime installer inside the zip: the MSVC runtime DLLs are copied
# next to the exe instead, so it runs on a PC that never had Visual Studio.
Invoke-Checked "$Qt\bin\windeployqt.exe" --release --qmldir qml --no-translations `
    --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime "$stage\Smalto.exe"
if ($env:VCToolsRedistDir) {
    $crt = Get-ChildItem "$env:VCToolsRedistDir\x64" -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
    if ($crt) { Copy-Item "$($crt.FullName)\*.dll" $stage }
}
# Smalto doesn't use SQL; these plugins link to database client libraries that
# aren't on a user's PC.
Remove-Item "$stage\sqldrivers" -Recurse -Force -ErrorAction SilentlyContinue
# Our QML imports QtQuick.Controls.Basic; these styles load only when picked
# explicitly. Windows and FluentWinUI3 stay: they're the platform default that a
# plain `import QtQuick.Controls` (Qt's own dialog code) resolves to.
foreach ($style in 'Material', 'Imagine', 'Universal', 'Fusion') {
    Remove-Item "$stage\qml\QtQuick\Controls\$style" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item "$stage\Qt6QuickControls2$style*.dll" -Force -ErrorAction SilentlyContinue
}
Copy-Item LICENSE, README.md $stage
# The manual (Help > Smalto Manual): CI builds it once (manual/book) for every package.
if (Test-Path manual\book\index.html) { Copy-Item manual\book "$stage\manual" -Recurse }

$zip = Join-Path $dist "Smalto-$version-windows-x64.zip"
Compress-Archive -Path $stage -DestinationPath $zip -Force
$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Wrote $zip ($mb MB)"
