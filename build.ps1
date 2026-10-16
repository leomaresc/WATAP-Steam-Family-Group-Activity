# Compila Steam Recent Play con MSVC + Ninja desde la linea de comandos.
# Uso:
#   powershell -ExecutionPolicy Bypass -File .\build.ps1            # Release
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Config Debug
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 -Test      # compila y ejecuta pruebas
param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

function Find-VsInstallDir {
    $candidates = @()
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $candidates += & $vswhere -latest -prerelease -products * -property installationPath
    }
    $candidates += Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\*\*" -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "VC\Auxiliary\Build\vcvars64.bat") } |
        ForEach-Object { $_.FullName }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c "VC\Auxiliary\Build\vcvars64.bat"))) { return $c }
    }
    throw "No se encontro Visual Studio con herramientas C++ (vcvars64.bat)."
}

$vsDir = Find-VsInstallDir
$vcvars = Join-Path $vsDir "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vsDir "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }
$ninja = Join-Path $vsDir "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

Write-Host "Visual Studio : $vsDir"
Write-Host "CMake         : $cmake"
Write-Host "Configuracion : $Config"

$buildDir = Join-Path $root "build\$Config"
if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

$ninjaArg = ""
if (Test-Path $ninja) { $ninjaArg = "-DCMAKE_MAKE_PROGRAM=`"$ninja`"" }

$configure = "call `"$vcvars`" >nul && `"$cmake`" -S `"$root`" -B `"$buildDir`" -G Ninja $ninjaArg -DCMAKE_BUILD_TYPE=$Config"
cmd /c $configure
if ($LASTEXITCODE -ne 0) { throw "Fallo la configuracion de CMake." }

$build = "call `"$vcvars`" >nul && `"$cmake`" --build `"$buildDir`" --config $Config"
cmd /c $build
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion." }

$exe = Join-Path $buildDir "SteamRecentPlay.exe"
if (Test-Path $exe) {
    $sizeKb = [math]::Round((Get-Item $exe).Length / 1KB, 1)
    Write-Host ""
    Write-Host "Ejecutable: $exe ($sizeKb KB)"
}

if ($Test) {
    Write-Host ""
    Write-Host "Ejecutando pruebas..."
    & (Join-Path $buildDir "srp_tests.exe")
    if ($LASTEXITCODE -ne 0) { throw "Las pruebas fallaron." }
}
