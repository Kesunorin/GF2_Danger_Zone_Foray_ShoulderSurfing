param([int]$TimeoutSeconds = 180, [switch]$WaitOnFailure)
$ErrorActionPreference = 'Stop'

# Shared by the launcher, manual attach, build and package scripts.
function Get-ModRelease {
    return [pscustomobject]@{ Version = '0.54.0'; Dll = 'GF2ShoulderSurfingV54.dll' }
}

function Write-LauncherLog([string]$Message) {
    $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') $Message"
    if ($script:LauncherLog) {
        $previous = [IO.Path]::ChangeExtension($script:LauncherLog, 'previous.log')
        if ((Test-Path -LiteralPath $script:LauncherLog) -and (Get-Item -LiteralPath $script:LauncherLog).Length -ge 1MB) {
            Move-Item -LiteralPath $script:LauncherLog -Destination $previous -Force
        }
        [IO.File]::AppendAllText($script:LauncherLog, "$line`r`n", [Text.UTF8Encoding]::new($false))
    }
    Write-Host $Message
}

function Get-InstalledGameExecutable {
    $key = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\GF2Exilium'
    $value = (Get-ItemProperty -LiteralPath $key -Name InstPath -ErrorAction Stop).InstPath
    if ([string]::IsNullOrWhiteSpace($value)) { throw '注册表 InstPath 为空。' }
    $installRoot = [IO.Path]::GetFullPath(([string]$value).Trim().Trim('"'))
    $executable = Join-Path $installRoot 'GF2 Game/GF2_Exilium.exe'
    if (!(Test-Path -LiteralPath $executable -PathType Leaf)) { throw "找不到游戏：$executable" }
    return $executable
}

function Get-TargetGame([string]$Executable) {
    $allGames = @(Get-Process -Name GF2_Exilium -ErrorAction SilentlyContinue)
    if ($allGames.Count -gt 1) { throw '检测到多个游戏进程，请只保留一个。' }
    if (!$allGames.Count) { return $null }
    if (![string]::Equals($allGames[0].Path, $Executable, [StringComparison]::OrdinalIgnoreCase)) {
        throw '正在运行的游戏与注册表安装目录不同。请退出旧进程后重试。'
    }
    return $allGames[0]
}

function Get-GameModModules($Game) {
    return @($Game.Modules | Where-Object { $_.ModuleName -like 'GF2ShoulderSurfing*.dll' })
}

function Wait-GameReady($Game, [datetime]$Deadline) {
    while ((Get-Date) -lt $Deadline) {
        $Game.Refresh()
        if ($Game.HasExited) { throw '游戏在等待期间退出。' }
        $modules = @($Game.Modules | ForEach-Object ModuleName)
        $age = ((Get-Date) - $Game.StartTime).TotalSeconds
        if ($modules -contains 'GameAssembly.dll' -and $modules -contains 'UnityPlayer.dll' -and
            $Game.MainWindowHandle -ne [IntPtr]::Zero -and $age -ge 10) { return }
        Start-Sleep -Milliseconds 500
    }
    throw '等待游戏启动超时。请确认游戏能正常启动。'
}

# Read only bytes appended after this loading attempt. Old READY messages may
# exist in a reused file and must never be treated as this attempt's success.
function Read-AppendedModLog([string]$Path, [long]$Offset) {
    if (!(Test-Path -LiteralPath $Path)) { return '' }
    $sharing = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $file = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $sharing)
    try {
        if ($file.Length -lt $Offset) { $Offset = 0 } # runtime log rotation
        [void]$file.Seek($Offset, [IO.SeekOrigin]::Begin)
        $reader = New-Object IO.StreamReader($file, [Text.Encoding]::UTF8, $true, 4096, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $file.Dispose() }
}

function Get-ModInitializationState([string]$Text, [int]$GameId) {
    $lines = @($Text -split '\r?\n' | Where-Object { $_ -match "PID=$GameId(?:\s|$)" })
    if ($lines | Where-Object { $_ -match 'INITIALIZATION FAILED:|ERROR:.*disabled' }) { return 'failed' }
    if ($lines | Where-Object { $_ -match '\bREADY:' }) { return 'ready' }
    return 'waiting'
}

function Wait-ModReady($Game, [string]$LogPath, [long]$Offset, [datetime]$Deadline) {
    while ((Get-Date) -lt $Deadline) {
        $Game.Refresh()
        if ($Game.HasExited) { throw '游戏在模组初始化期间退出。请保留运行日志。' }
        $text = Read-AppendedModLog $LogPath $Offset
        $state = Get-ModInitializationState $text $Game.Id
        if ($state -eq 'failed') { throw "模组初始化失败，请查看：$LogPath" }
        if ($state -eq 'ready') { return }
        Start-Sleep -Milliseconds 250
    }
    throw "等待模组 READY 超时。请查看 $LogPath，确认登录或资源更新已完成；不要重复加载。"
}

function Invoke-ModAttach([int]$GameId) {
    & (Join-Path $PSScriptRoot 'Attach-Mod.ps1') -GamePid $GameId | ForEach-Object { Write-LauncherLog ([string]$_) }
    if (!$?) { throw '加载脚本失败。' }
}

function Invoke-OneClickStartup {
    $script:LauncherLog = Join-Path $PSScriptRoot 'build/GF2ShoulderSurfing-launcher.log'
    try {
        $build = Join-Path $PSScriptRoot 'build'
        if (!(Test-Path -LiteralPath $build -PathType Container)) { throw '缺少 build 目录，请先解压完整发布包或编译。' }
        Write-LauncherLog '开始一键启动。'
        $release = Get-ModRelease
        $dll = Join-Path $build $release.Dll
        if (!(Test-Path -LiteralPath $dll) -or !(Test-Path -LiteralPath (Join-Path $build 'GF2Attach.exe'))) { throw '缺少模组 DLL 或加载器，请使用完整发布包。' }
        $executable = Get-InstalledGameExecutable
        Write-LauncherLog "版本=$($release.Version) 游戏=$executable"
        Write-LauncherLog '加载后校验当前运行时接口、ABI 和绑定；以当前进程的 READY 为准。'
        $game = Get-TargetGame $executable
        if (!$game) {
            Write-LauncherLog '正在启动游戏……'
            # The game is an interactive application the user explicitly wants
            # to open; no visible helper or service is launched.
            $game = Start-Process -FilePath $executable -WorkingDirectory (Split-Path -Parent $executable) -PassThru
        } else { Write-LauncherLog "复用已运行游戏 PID=$($game.Id)。" }
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        Wait-GameReady $game $deadline
        $loaded = @(Get-GameModModules $game)
        if ($loaded | Where-Object { $_.ModuleName -ne $release.Dll }) { throw '游戏已加载其他版本，请正常退出并重启后再执行。' }
        $logPath = [IO.Path]::ChangeExtension($dll, '.log')
        [long]$offset = 0
        if ($loaded.Count) {
            if ([IO.Path]::GetFullPath($loaded[0].FileName) -ne [IO.Path]::GetFullPath($dll)) { throw '相同名称模组已从另一目录加载，请重启游戏。' }
            # Already loaded: check its READY for the current process instead
            # of increasing LoadLibrary reference counts or resetting camera.
            Write-LauncherLog '当前模组已加载，检查初始化状态。'
        } else {
            if (Test-Path -LiteralPath $logPath) { $offset = (Get-Item -LiteralPath $logPath).Length }
            Write-LauncherLog "开始加载 $($release.Dll)，PID=$($game.Id)。"
            Invoke-ModAttach $game.Id
        }
        Wait-ModReady $game $logPath $offset $deadline
        Write-LauncherLog "SUCCESS：模组 READY，PID=$($game.Id)。F7 开启肩视角，F10 设置。"
        return 0
    } catch {
        $message = "FAILED：$($_.Exception.Message)"
        try { Write-LauncherLog $message } catch { Write-Host $message }
        if ($WaitOnFailure) { [void](Read-Host '启动失败；按回车关闭此窗口，日志保存在 build 目录') }
        return 1
    }
}

if ($MyInvocation.InvocationName -ne '.') { exit (Invoke-OneClickStartup) }
