# update.ps1: the weekly tool update (a SYSTEM scheduled task, \TempoCI\Update, runs it). Waits until no
# job is running, stops the runners so none starts mid-update, upgrades the tools the jobs use, and starts
# the runners again.
#
# It runs as SYSTEM, so it is installed where the CI user cannot write (C:\Program Files\TempoCI) and reads
# nothing the CI user can write: jobs run PR code as that user. For the same reason it does not post to
# Slack itself (the webhook is the CI user's): it writes its result to C:\ProgramData\TempoCI\update-status,
# which the CI user can read but not write, and on failure starts the daily check, which reports it.
#
# Windows and Visual Studio are not updated here: they change the toolchain under every build, so daily.sh
# reports pending Windows updates and a human installs them.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
$StatusDir = 'C:\ProgramData\TempoCI'
$Tools = 'C:\ci-tools'
# The tools the jobs use. Everything else on the machine is whoever installed it's to update.
$Packages = @('Git.Git', 'Kitware.CMake', 'GitHub.cli', 'astral-sh.uv', 'jqlang.jq')
$MaxWaitMinutes = if ($env:TEMPO_CI_UPDATE_MAX_WAIT_MINUTES) { [int]$env:TEMPO_CI_UPDATE_MAX_WAIT_MINUTES } else { 360 }

function Log($text) { "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $text" | Tee-Object -FilePath "$StatusDir\update.log" -Append }
function Status($text) {
    Set-Content -Path "$StatusDir\update-status" -Value "$text ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))" -Encoding ASCII
    if ($text -notlike 'ok*') { Start-ScheduledTask -TaskPath '\TempoCI\' -TaskName 'Daily' -ErrorAction SilentlyContinue }
}

$waited = 0
while (Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue) {
    if ($waited -ge $MaxWaitMinutes) {
        Log "skipped: a job was running for all of $MaxWaitMinutes minutes"
        Status "skipped: a job was running for all of $MaxWaitMinutes minutes"
        exit 1
    }
    Start-Sleep -Seconds 300
    $waited += 5
}

$services = @(Get-Service -Name 'actions.runner.*' -ErrorAction SilentlyContinue)
Log "update: stopping $($services.Name -join ', ')"
$services | Where-Object Status -eq 'Running' | Stop-Service -Force

$failures = @()
try {
    # SYSTEM has no winget alias of its own: run App Installer's binary directly.
    $winget = Get-ChildItem 'C:\Program Files\WindowsApps\Microsoft.DesktopAppInstaller_*_x64__8wekyb3d8bbwe\winget.exe' -ErrorAction SilentlyContinue |
        Sort-Object FullName | Select-Object -Last 1 -ExpandProperty FullName
    if (-not $winget) {
        $failures += 'winget (not found)'
    } else {
        foreach ($id in $Packages) {
            Log "update: winget upgrade $id"
            & $winget upgrade --id $id --exact --silent --scope machine --disable-interactivity `
                --accept-source-agreements --accept-package-agreements 2>&1 | ForEach-Object { Log "  $_" }
            # 0x8A15002B: no applicable upgrade (already current).
            if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { $failures += "$id ($LASTEXITCODE)" }
        }
    }
    # The jobs run copies of winget's portable tools (see bootstrap.ps1's Sync-PortableTools): refresh them.
    foreach ($link in Get-ChildItem 'C:\Program Files\WinGet\Links' -Filter '*.exe' -ErrorAction SilentlyContinue) {
        if ($link.Name -notmatch '^(uv|uvx|uvw|jq)\.exe$') { continue }
        $target = if ($link.LinkType) { @($link.Target)[0] } else { $link.FullName }
        try { Copy-Item -LiteralPath $target -Destination "$Tools\bin\$($link.Name)" -Force -ErrorAction Stop }
        catch { $failures += "copy $($link.Name)" }
    }
    if (Test-Path "$Tools\cargo\bin\rustup.exe") {
        Log 'update: rustup update stable'
        $env:RUSTUP_HOME = "$Tools\rustup"; $env:CARGO_HOME = "$Tools\cargo"
        & "$Tools\cargo\bin\rustup.exe" update stable --no-self-update 2>&1 | ForEach-Object { Log "  $_" }
        if ($LASTEXITCODE -ne 0) { $failures += 'rustup' }
    }
} finally {
    foreach ($s in $services) {
        Start-Service -Name $s.Name -ErrorAction SilentlyContinue
        if ((Get-Service -Name $s.Name).Status -ne 'Running') { $failures += "restart $($s.Name)" }
    }
}

if ($failures.Count -gt 0) {
    Log "update: failed: $($failures -join ', ')"
    Status "failed: $($failures -join ', ')"
    exit 1
}
Log 'update: done'
Status 'ok'
