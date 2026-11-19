param([int]$GamePid = 0)
$ErrorActionPreference = 'Stop'
$buildDir = Join-Path $PSScriptRoot 'build'
$loader = Join-Path $buildDir 'GF2Attach.exe'
. (Join-Path $PSScriptRoot 'Start-GameAndMod.ps1')
$release = Get-ModRelease
$dll = Join-Path $buildDir $release.Dll
if (!(Test-Path -LiteralPath $loader) -or !(Test-Path -LiteralPath $dll)) { throw 'Run Build.ps1 first.' }
$gameProcesses = @(Get-Process -Name GF2_Exilium -ErrorAction Stop)
if ($GamePid -eq 0) {
    if ($gameProcesses.Count -ne 1) { throw 'Multiple games running. Specify -GamePid.' }
    $GamePid = $gameProcesses[0].Id
}
$selectedGame = $gameProcesses | Where-Object Id -EQ $GamePid
if (!$selectedGame) { throw 'The specified PID is not a GF2_Exilium process.' }
Write-Host "Version=$($release.Version); runtime API, ABI and binding validation required."
$loaded = @($selectedGame.Modules | Where-Object { $_.ModuleName -like 'GF2ShoulderSurfing*.dll' })
if ($loaded | Where-Object { $_.ModuleName -ne $release.Dll -or [IO.Path]::GetFullPath($_.FileName) -ne [IO.Path]::GetFullPath($dll) }) {
    throw 'Another mod version/path is loaded. Exit and restart the game first.'
}
$logPath = [IO.Path]::ChangeExtension($dll, '.log')
[long]$offset = 0
if (!$loaded.Count -and (Test-Path -LiteralPath $logPath)) { $offset = (Get-Item -LiteralPath $logPath).Length }
& $loader $dll $GamePid
if ($LASTEXITCODE -ne 0) { throw "Attach failed, exit code $LASTEXITCODE" }
Wait-ModReady $selectedGame $logPath $offset ((Get-Date).AddSeconds(180))
Write-Host "READY: runtime initialized for PID=$GamePid. Initialization log: $logPath"
