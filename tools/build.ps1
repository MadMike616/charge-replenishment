param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$SdkDirectory,
    [Parameter(Mandatory=$true)][string]$VsDevCmd
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$sdkRoot = [IO.Path]::GetFullPath($SdkDirectory)
if ($buildRoot.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $buildRoot -eq $sourceRoot) {
    throw 'Build output must be outside the plugin source tree.'
}
if (!(Test-Path -LiteralPath $VsDevCmd) -or !(Test-Path -LiteralPath (Join-Path $sdkRoot 'include\D2RLPlugin\api.h'))) { throw 'MSVC toolchain or SDK not found.' }
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$common = "/nologo /std:c++20 /W4 /WX /permissive- /EHsc /utf-8 /O2 /Ob2 /DNDEBUG /MT /Brepro /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /I`"$sdkRoot\include`""
$batch = @"
@echo off
setlocal
call "$VsDevCmd" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
cd /d "$sourceRoot"
cl.exe $common /c src\plugin.cpp /Fo"$buildRoot\plugin.obj"
if errorlevel 1 exit /b 1
cl.exe $common /c src\native.cpp /Fo"$buildRoot\native.obj"
if errorlevel 1 exit /b 1
cd /d "$sourceRoot\src"
rc.exe /nologo /I"$sdkRoot\include" /fo"$buildRoot\plugin.res" plugin.rc
if errorlevel 1 exit /b 1
link.exe /nologo /DLL /INCREMENTAL:NO /Brepro /MACHINE:X64 /OPT:REF /OPT:ICF /OUT:"$buildRoot\d2rl-charge-replenishment.dll" "$buildRoot\plugin.obj" "$buildRoot\native.obj" "$buildRoot\plugin.res"
if errorlevel 1 exit /b 1
cd /d "$sourceRoot"
cl.exe $common tests\policy_tests.cpp /Fo"$buildRoot\policy_tests.obj" /Fe"$buildRoot\policy_tests.exe" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
cl.exe $common tests\runtime_tests.cpp /Fo"$buildRoot\runtime_tests.obj" /Fe"$buildRoot\runtime_tests.exe" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
cl.exe $common tests\native_stats_tests.cpp /Fo"$buildRoot\native_stats_tests.obj" /Fe"$buildRoot\native_stats_tests.exe" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
"$buildRoot\policy_tests.exe"
if errorlevel 1 exit /b 1
"$buildRoot\runtime_tests.exe"
if errorlevel 1 exit /b 1
"$buildRoot\native_stats_tests.exe"
if errorlevel 1 exit /b 1
"@
$batchPath = Join-Path $buildRoot 'build-release.cmd'
[IO.File]::WriteAllText($batchPath, $batch, [Text.UTF8Encoding]::new($false))
& cmd.exe /d /c $batchPath
if ($LASTEXITCODE -ne 0) { throw "Release build or tests failed (exit $LASTEXITCODE)." }
$dll = Join-Path $buildRoot 'd2rl-charge-replenishment.dll'
$version = (Get-Item -LiteralPath $dll).VersionInfo
if ($version.FileVersion -ne '1.0.0' -or $version.ProductVersion -ne '1.0.0' -or $version.CompanyName -ne 'MadMike') { throw 'Release metadata mismatch.' }
Get-Item -LiteralPath $dll | Select-Object FullName,@{n='FileVersion';e={$_.VersionInfo.FileVersion}},@{n='ProductVersion';e={$_.VersionInfo.ProductVersion}},@{n='Author';e={$_.VersionInfo.CompanyName}}
Get-FileHash -LiteralPath $dll -Algorithm SHA256
