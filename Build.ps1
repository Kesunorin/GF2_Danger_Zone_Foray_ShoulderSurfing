param([string]$MsvcRoot = $env:VCToolsInstallDir, [string]$SdkRoot = $env:WindowsSdkDir, [string]$SdkVersion = $env:WindowsSDKVersion)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$outDir = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
if (!$MsvcRoot -or !$SdkRoot) { throw 'Use an x64 Visual Studio developer shell, or supply -MsvcRoot and -SdkRoot.' }
if (!$SdkVersion) {
    $SdkVersion = (Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'Include') -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
$SdkVersion = $SdkVersion.TrimEnd('\','/')
$compiler = Join-Path $MsvcRoot 'bin/Hostx64/x64/cl.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw "Compiler not found: $compiler" }
$env:PATH = (Split-Path -Parent $compiler) + ';' + $env:PATH
$includes = @("/I$MsvcRoot/include", "/I$SdkRoot/Include/$SdkVersion/ucrt", "/I$SdkRoot/Include/$SdkVersion/shared", "/I$SdkRoot/Include/$SdkVersion/um")
$libs = @("/LIBPATH:$MsvcRoot/lib/x64", "/LIBPATH:$SdkRoot/Lib/$SdkVersion/ucrt/x64", "/LIBPATH:$SdkRoot/Lib/$SdkVersion/um/x64")
$flags = @('/nologo','/O2','/MT','/W4','/EHsc','/std:c++17','/utf-8') + $includes
. (Join-Path $projectRoot 'Start-GameAndMod.ps1')
$release = Get-ModRelease
$native = Join-Path $projectRoot 'vendor/minhook/src'
$sources = @((Join-Path $projectRoot 'src/ShoulderSurfing.cpp'), "$native/buffer.c", "$native/hook.c", "$native/trampoline.c", "$native/hde/hde64.c")
Push-Location -LiteralPath $outDir
try {
    & $compiler @flags /LD @sources "/Fe:$($release.Dll)" /link @libs user32.lib gdi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'DLL build failed' }
    & $compiler @flags (Join-Path $projectRoot 'src/Loader.cpp') '/Fe:GF2Attach.exe' /link @libs
    if ($LASTEXITCODE -ne 0) { throw 'Loader build failed' }
} finally { Pop-Location }
Write-Host "Build complete: $outDir"