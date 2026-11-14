# bootstrap.ps1: set up (or repair) a Windows Tempo CI machine. Run it elevated (an administrator
# PowerShell), from this checkout. It is idempotent, so re-running it after changing anything here is how
# a change reaches the machine.
#
#   powershell -ExecutionPolicy Bypass -File CI\Machines\windows\bootstrap.ps1            everything below
#   powershell -ExecutionPolicy Bypass -File CI\Machines\windows\bootstrap.ps1 runners    only the runners step
#
# The first run creates the CI user and asks for its password in this window. Later runs ask for it again
# only when a scheduled task's definition has changed (the tasks store it, as the runner services do).
# What it cannot do is listed at the end, as checks: the firmware's power-on-after-power-loss setting, and
# the alerting secrets. See CI/Machines/README.md.
#
# Layout (see README.md): the CI user's runners, scripts and secrets live in its profile; the tools the
# jobs use, and the root half of the weekly update, live where the CI user can read but not write, because
# jobs run PR code as that user.
#Requires -RunAsAdministrator
[CmdletBinding()]
param([ValidateSet('all', 'runners')] [string] $Step = 'all')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest's progress bar makes downloads crawl
$Here = $PSScriptRoot
$Machines = Split-Path $Here
$CiUser = if ($env:TEMPO_CI_USER) { $env:TEMPO_CI_USER } else { 'ci' }
$Tools = 'C:\ci-tools'                       # python3, Rust: read-only to the CI user
$CiRoot = 'C:\ci'                            # TEMPO_CI_ROOT: the build trees
$LibExec = 'C:\Program Files\TempoCI'        # update.ps1, run as SYSTEM
$StatusDir = 'C:\ProgramData\TempoCI'        # update.ps1's log and status, readable by the CI user
$GitDir = 'C:\Program Files\Git'
$PythonVersion = '3.12'
# Visual Studio 2022 17.14 (MSVC 14.44): the toolchain both UE 5.7 and 5.8 accept
# (Engine/Config/Windows/Windows_SDK.json). Not the 14.44 "LTSC" component the engines suggest: it pins
# 14.44.35207, which the same file bans. VC.Tools.x86.x64 is the newest 14.44.
$VsComponents = @(
    'Microsoft.VisualStudio.Workload.VCTools',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.Windows11SDK.22621',
    'Microsoft.Net.Component.4.6.2.TargetingPack'
)
# The jobs' tools from winget, machine-wide. Only missing ones are installed: upgrading one under a running
# job is update.ps1's job, which waits for the runners to be idle.
$WingetPackages = @('Git.Git', 'Kitware.CMake', 'GitHub.cli', 'astral-sh.uv', 'jqlang.jq')

New-Item -ItemType Directory -Force $StatusDir | Out-Null
# Re-inherit the folder's ACL first: an earlier bootstrap could leave files here with an empty one.
if (Get-ChildItem $StatusDir -Force | Select-Object -First 1) { & icacls.exe "$StatusDir\*" /reset /T /C /Q | Out-Null }
Start-Transcript -Path "$StatusDir\bootstrap.log" -Append | Out-Null

function Step($text) { Write-Host "`n== $text" -ForegroundColor Cyan }

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

public static class TempoCiNative {
    [DllImport("userenv.dll", CharSet = CharSet.Unicode)]
    static extern int CreateProfile(string sid, string name, StringBuilder path, uint len);

    // Create a user's profile without logging them on (so without their password).
    public static void EnsureProfile(string sid, string name) {
        var path = new StringBuilder(260);
        int hr = CreateProfile(sid, name, path, (uint)path.Capacity);
        if (hr != 0 && hr != unchecked((int)0x800700B7)) Marshal.ThrowExceptionForHR(hr);   // B7: exists
    }

    [StructLayout(LayoutKind.Sequential)]
    struct LSA_UNICODE_STRING { public ushort Length; public ushort MaximumLength; public IntPtr Buffer; }
    [StructLayout(LayoutKind.Sequential)]
    struct LSA_OBJECT_ATTRIBUTES {
        public int Length; public IntPtr RootDirectory; public IntPtr ObjectName; public uint Attributes;
        public IntPtr SecurityDescriptor; public IntPtr SecurityQualityOfService;
    }
    [DllImport("advapi32.dll")]
    static extern uint LsaOpenPolicy(IntPtr system, ref LSA_OBJECT_ATTRIBUTES attrs, uint access, out IntPtr handle);
    [DllImport("advapi32.dll")]
    static extern uint LsaAddAccountRights(IntPtr handle, byte[] sid, LSA_UNICODE_STRING[] rights, uint count);
    [DllImport("advapi32.dll")]
    static extern uint LsaClose(IntPtr handle);
    [DllImport("advapi32.dll")]
    static extern int LsaNtStatusToWinError(uint status);

    // Grant a user right (SeServiceLogonRight, SeBatchLogonRight, ...). Granting one already held is a no-op.
    public static void AddRight(string sidString, string right) {
        var sid = new SecurityIdentifier(sidString);
        var bytes = new byte[sid.BinaryLength];
        sid.GetBinaryForm(bytes, 0);
        var attrs = new LSA_OBJECT_ATTRIBUTES();
        attrs.Length = Marshal.SizeOf(attrs);
        IntPtr policy;
        uint status = LsaOpenPolicy(IntPtr.Zero, ref attrs, 0x000F0FFF, out policy);
        if (status != 0) throw new Win32Exception(LsaNtStatusToWinError(status));
        var name = new LSA_UNICODE_STRING();
        name.Buffer = Marshal.StringToHGlobalUni(right);
        name.Length = (ushort)(right.Length * 2);
        name.MaximumLength = (ushort)(name.Length + 2);
        try {
            status = LsaAddAccountRights(policy, bytes, new[] { name }, 1);
            if (status != 0) throw new Win32Exception(LsaNtStatusToWinError(status));
        } finally {
            Marshal.FreeHGlobal(name.Buffer);
            LsaClose(policy);
        }
    }
}
'@

# Git Bash's form of a Windows path: C:\Program Files\Git -> /c/Program Files/Git.
function BashPath($p) { '/' + $p.Substring(0, 1).ToLower() + ($p.Substring(2) -replace '\\', '/') }

# Directory owned by Administrators and SYSTEM, readable (not writable) by everyone else. A folder made
# at the root of C: otherwise inherits "Authenticated Users: Modify".
function Protect-Dir($dir) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    # The folder alone gets the (inheritable) entries, then everything under it is reset to inherit them.
    # Not one icacls /T: a file cannot take (OI)(CI) entries, and /T would strip its inherited ones anyway,
    # leaving every file with an empty ACL that no one, SYSTEM included, can read.
    & icacls.exe $dir /inheritance:r /grant:r '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' /Q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls failed on $dir" }
    if (Get-ChildItem $dir -Force | Select-Object -First 1) {
        & icacls.exe "$dir\*" /reset /T /C /Q | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "icacls /reset failed under $dir" }
    }
}

# A file only the CI user (and, as ever, administrators and SYSTEM) can read.
function Protect-Secret($file) {
    & icacls.exe $file /inheritance:r /grant:r "${CiUser}:(R)" '*S-1-5-32-544:F' '*S-1-5-18:F' /Q | Out-Null
}

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Get-CiSid { (New-Object Security.Principal.NTAccount($CiUser)).Translate([Security.Principal.SecurityIdentifier]).Value }
function Get-CiHome { (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList\$(Get-CiSid)").ProfileImagePath }

# The PATH every job gets (each runner's .path), Git's bash ahead of System32's (WSL) one. The scripts get
# the same, in Git Bash's form ($CiTempo\path, read by lib.sh).
function Get-JobPath($ciHome) {
    @(
        "$Tools\bin", "$Tools\cargo\bin", "$ciHome\.local\bin",
        "$GitDir\cmd", "$GitDir\bin", "$GitDir\usr\bin",
        'C:\Program Files\CMake\bin', 'C:\Program Files\GitHub CLI', 'C:\Program Files\WinGet\Links',
        'C:\Windows\system32', 'C:\Windows', 'C:\Windows\System32\Wbem',
        'C:\Windows\System32\WindowsPowerShell\v1.0', 'C:\Windows\System32\OpenSSH'
    )
}

function Power-And-Updates {
    Step '1. Power: never sleep or hibernate; Windows Update: download, never install or restart on its own'
    foreach ($setting in 'standby-timeout-ac', 'hibernate-timeout-ac', 'disk-timeout-ac', 'standby-timeout-dc', 'hibernate-timeout-dc') {
        & powercfg.exe /change $setting 0
    }
    & powercfg.exe /hibernate off   # also turns off Fast Startup, so a restart is a real boot
    # AUOptions 3: download automatically, a human picks when to install (daily.sh reports each pending
    # update). An OS update changes what every build runs on, as macOS and Xcode updates do on the Mac.
    $au = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
    New-Item -Force $au | Out-Null
    Set-ItemProperty $au -Name NoAutoUpdate -Value 0 -Type DWord
    Set-ItemProperty $au -Name AUOptions -Value 3 -Type DWord
    Set-ItemProperty $au -Name NoAutoRebootWithLoggedOnUsers -Value 1 -Type DWord

    Step '2. Long paths: Windows and Git'
    Set-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1 -Type DWord
}

function Ci-User {
    Step "3. The CI user '$CiUser': standard user, password never expires, allowed to run services and tasks"
    if (-not (Get-LocalUser -Name $CiUser -ErrorAction SilentlyContinue)) {
        Write-Host "Creating '$CiUser'. Choose its password: the runner services log on with it, and"
        Write-Host "register-runner.ps1 asks for it again. Nothing stores it but Windows."
        while ($true) {
            $pw = Read-Host "Password for $CiUser" -AsSecureString
            $pw2 = Read-Host "Again" -AsSecureString
            $a = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($pw))
            $b = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($pw2))
            $same = ($a -ceq $b) -and $a.Length -gt 0
            $b = $null
            if ($same) { $script:CiPassword = $a; $a = $null; break }
            $a = $null
            Write-Host 'They differ (or are empty); again.'
        }
        New-LocalUser -Name $CiUser -Password $pw -FullName 'Tempo CI' -Description 'Runs the Tempo CI runners and their jobs' `
            -PasswordNeverExpires -AccountNeverExpires -UserMayNotChangePassword | Out-Null
        Add-LocalGroupMember -SID 'S-1-5-32-545' -Member $CiUser   # Users
    }
    $sid = Get-CiSid
    [TempoCiNative]::EnsureProfile($sid, $CiUser)
    [TempoCiNative]::AddRight($sid, 'SeServiceLogonRight')   # the runner services
    [TempoCiNative]::AddRight($sid, 'SeBatchLogonRight')     # the scheduled tasks
    Write-Host "$CiUser ($sid), profile $(Get-CiHome)"
}

function Tools {
    Step "4. Tools: Visual Studio Build Tools, $($WingetPackages -join ', '), Python $PythonVersion and Rust in $Tools"
    $winget = (Get-Command winget.exe -ErrorAction SilentlyContinue).Source
    if (-not $winget) { throw 'winget is missing: install App Installer from the Microsoft Store.' }
    $common = @('--exact', '--silent', '--disable-interactivity', '--accept-source-agreements', '--accept-package-agreements')

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = if (Test-Path $vswhere) { & $vswhere -products Microsoft.VisualStudio.Product.BuildTools -version '[17.14,18.0)' -property installationPath } else { $null }
    $add = ($VsComponents | ForEach-Object { "--add $_" }) -join ' '
    if (-not $vs) {
        Write-Host 'Installing Visual Studio 2022 Build Tools (this takes a while)'
        & $winget install --id Microsoft.VisualStudio.2022.BuildTools @common --override "--quiet --wait --norestart --nocache $add"
        if ($LASTEXITCODE -ne 0) { throw "Visual Studio Build Tools failed to install ($LASTEXITCODE)." }
    } else {
        # Present: make sure it has every component (a no-op when it does).
        $installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"
        $p = Start-Process -FilePath $installer -ArgumentList "modify --installPath `"$vs`" --quiet --norestart $add" -Wait -PassThru
        if ($p.ExitCode -notin 0, 3010) { throw "Visual Studio Installer modify failed ($($p.ExitCode))." }
    }

    foreach ($id in $WingetPackages) {
        & $winget list --id $id --exact --accept-source-agreements --disable-interactivity | Out-Null
        if ($LASTEXITCODE -eq 0) { continue }
        Write-Host "Installing $id"
        & $winget install --id $id --scope machine @common
        if ($LASTEXITCODE -ne 0) { throw "winget install $id failed ($LASTEXITCODE)." }
    }
    & git.exe config --system core.longpaths true
    Refresh-Path

    Protect-Dir $Tools
    # python3 for the jobs (the Check Machine step, Tempo's scripts). uv's own, not python.org's, which has
    # no python3.exe, and not the Store's alias, which is per user. The tests install their own with uv.
    $env:UV_PYTHON_INSTALL_DIR = "$Tools\python"
    $env:UV_PYTHON_BIN_DIR = "$Tools\bin"
    if (-not (Test-Path "$Tools\bin\python3.exe")) {
        & uv.exe python install $PythonVersion
        if ($LASTEXITCODE -ne 0) { throw "uv python install $PythonVersion failed." }
        Copy-Item "$Tools\bin\python$PythonVersion.exe" "$Tools\bin\python3.exe"
    }
    # Rust: toolchains in $Tools (updated by update.ps1, as SYSTEM); each job's CARGO_HOME (registry,
    # builds) stays the CI user's own ~\.cargo, so only RUSTUP_HOME is set for the jobs.
    if (-not (Test-Path "$Tools\cargo\bin\cargo.exe")) {
        $init = Join-Path $env:TEMP 'rustup-init.exe'
        Invoke-WebRequest -UseBasicParsing 'https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe' -OutFile $init
        $env:RUSTUP_HOME = "$Tools\rustup"; $env:CARGO_HOME = "$Tools\cargo"
        & $init -y --no-modify-path --profile minimal --default-toolchain stable
        if ($LASTEXITCODE -ne 0) { throw 'rustup-init failed.' }
        Remove-Item $init
        Remove-Item Env:\CARGO_HOME
    }
    Protect-Dir $Tools   # again: re-apply to what the installers just wrote
}

function Ci-Files {
    $ciHome = Get-CiHome
    $ciTempo = "$ciHome\tempo-ci"
    Step "5. $ciTempo (scripts, machine contract) and $CiRoot (build trees)"
    foreach ($d in 'bin', 'logs', 'state') { New-Item -ItemType Directory -Force "$ciTempo\$d" | Out-Null }
    foreach ($f in 'lib.sh', 'notify.sh', 'heartbeat.sh', 'daily.sh') { Copy-Item "$Machines\$f" "$ciTempo\bin\$f" -Force }
    if (-not (Test-Path "$ciTempo\machine.env")) {
        Copy-Item "$Here\machine.env.example" "$ciTempo\machine.env"
        Write-Host "Wrote $ciTempo\machine.env from the example: check it for this machine's engines."
    }
    $bashPath = (@('/mingw64/bin', '/usr/bin') + (Get-JobPath $ciHome | ForEach-Object { BashPath $_ })) -join ':'
    Set-Content -Path "$ciTempo\path" -Value $bashPath -Encoding ASCII -NoNewline
    foreach ($s in 'slack-webhook', 'healthchecks-url') { if (Test-Path "$ciTempo\$s") { Protect-Secret "$ciTempo\$s" } }
    # What admin wrote here is admin's: give the CI user its own files back.
    & icacls.exe $ciTempo /setowner $CiUser /T /C /Q | Out-Null

    New-Item -ItemType Directory -Force $CiRoot | Out-Null
    & icacls.exe $CiRoot /inheritance:r /grant:r "${CiUser}:(OI)(CI)M" '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-18:(OI)(CI)F' /Q | Out-Null
    & icacls.exe $CiRoot /setowner $CiUser /Q | Out-Null
}

function Housekeeping {
    $ciHome = Get-CiHome
    $ciTempo = BashPath "$ciHome\tempo-ci"
    Step '6. Tasks: heartbeat (every 5 min), daily check (07:00), as the CI user; weekly update (Sun 03:00), as SYSTEM'
    $bash = "$GitDir\bin\bash.exe"
    # The CI user's tasks store its password, as its runner services do: Windows refuses an admin an S4U
    # ("do not store password") task for another user. So a task is only re-registered, and the password
    # only asked for, when the task's definition (stamped into its description) has changed.
    function CiTask($name, $script, $triggers, $triggerSpec, $limit) {
        $log = "$ciTempo/logs/$($script -replace '\.sh$', '').log"
        $arguments = "-c `"'$ciTempo/bin/$script' >> '$log' 2>&1`""
        $stamp = "tempo-ci: $bash $arguments | $triggerSpec | $limit | $CiUser"
        $existing = Get-ScheduledTask -TaskPath '\TempoCI\' -TaskName $name -ErrorAction SilentlyContinue
        if ($existing -and $existing.Description -eq $stamp) { Write-Host "  $name`: unchanged"; return }
        if (-not $script:CiPassword) {
            $script:CiPassword = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR(
                (Read-Host "Password of $CiUser (its scheduled tasks log on with it)" -AsSecureString)))
        }
        $action = New-ScheduledTaskAction -Execute $bash -Argument $arguments
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
            -MultipleInstances IgnoreNew -ExecutionTimeLimit $limit
        Register-ScheduledTask -TaskName $name -TaskPath '\TempoCI\' -Description $stamp -Action $action -Trigger $triggers `
            -Settings $settings -User "$env:COMPUTERNAME\$CiUser" -Password $script:CiPassword -RunLevel Limited -Force | Out-Null
        Write-Host "  $name`: registered"
    }
    $every5 = New-ScheduledTaskTrigger -Once -At (Get-Date).Date -RepetitionInterval (New-TimeSpan -Minutes 5)
    CiTask 'Heartbeat' 'heartbeat.sh' @($every5, (New-ScheduledTaskTrigger -AtStartup)) 'every 5 min + at startup' (New-TimeSpan -Minutes 4)
    CiTask 'Daily' 'daily.sh' @(New-ScheduledTaskTrigger -Daily -At '07:00') 'daily 07:00' (New-TimeSpan -Hours 2)
    $script:CiPassword = $null

    # SYSTEM's half: installed where the CI user (and so a PR) cannot write.
    Protect-Dir $LibExec
    Copy-Item "$Here\update.ps1" "$LibExec\update.ps1" -Force
    Protect-Dir $StatusDir
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$LibExec\update.ps1`""
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
        -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Hours 8)
    Register-ScheduledTask -TaskName 'Update' -TaskPath '\TempoCI\' -Action $action `
        -Trigger (New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At '03:00') `
        -Principal (New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest) `
        -Settings $settings -Force | Out-Null
}

function Runners {
    $ciHome = Get-CiHome
    Step '7. Runners: environment, PATH and service settings for each registered runner'
    $path = (Get-JobPath $ciHome) -join ';'
    # PATH goes in .env too: on Windows the service does not read .path (runsvc.sh does that, on Linux and
    # Mac), but the runner loads .env into its own environment, which every job inherits.
    $runnerEnv = @(
        "PATH=$path",
        'TEMPO_CI_MACHINE_ENV=' + ("$ciHome\tempo-ci\machine.env" -replace '\\', '/'),
        "RUSTUP_HOME=$Tools\rustup",
        'PYTHONUTF8=1'
    ) -join "`r`n"   # CRLF: the Windows runner splits .env on CRLF, so with LF it is all one PATH= line
    $found = $false
    foreach ($dir in Get-ChildItem "$ciHome\actions-runner*" -Directory -ErrorAction SilentlyContinue) {
        if (-not (Test-Path "$($dir.FullName)\.runner")) { continue }
        $found = $true
        $name = (Get-Content "$($dir.FullName)\.runner" -Raw | ConvertFrom-Json).agentName
        $svc = if (Test-Path "$($dir.FullName)\.service") { (Get-Content "$($dir.FullName)\.service" -Raw).Trim() } else { $null }
        Write-Host "$name ($($dir.FullName)), service $svc"
        $changed = $false
        foreach ($pair in @(@('.env', $runnerEnv), @('.path', $path))) {
            $file = "$($dir.FullName)\$($pair[0])"
            $old = if (Test-Path $file) { (Get-Content $file -Raw) } else { '' }
            if ($old -ne $pair[1]) { Set-Content -Path $file -Value $pair[1] -Encoding ASCII -NoNewline; $changed = $true }
        }
        if (-not $svc) { Write-Host "  ! no .service: register it with register-runner.ps1"; continue }
        # Restart on failure, however often (config.cmd's recovery gives up after its third), and start at
        # boot: config.cmd's delayed auto-start, so the network is up first.
        & sc.exe failure $svc reset= 86400 actions= restart/60000/restart/60000/restart/60000 | Out-Null
        & sc.exe config $svc start= delayed-auto | Out-Null
        $busy = Get-CimInstance Win32_Process -Filter "Name = 'Runner.Worker.exe'" |
            Where-Object { $_.ExecutablePath -like "$($dir.FullName)\*" }
        $state = (Get-Service $svc).Status
        if ($busy) {
            if ($changed) { Write-Host "  $name is running a job: its new environment applies from its next restart." }
        } elseif ($changed -or $state -ne 'Running') {
            Restart-Service $svc -Force
        }
    }
    if (-not $found) { Write-Host 'No registered runners yet: run register-runner.ps1.' }
}

function Checks {
    Step '8. Checks (fix these by hand; README.md says how)'
    $issues = New-Object System.Collections.Generic.List[string]
    $ciTempo = "$(Get-CiHome)\tempo-ci"
    if (-not (Test-Path "$ciTempo\slack-webhook")) { $issues.Add("no $ciTempo\slack-webhook: alerts go nowhere") }
    if (-not (Test-Path "$ciTempo\healthchecks-url")) { $issues.Add("no $ciTempo\healthchecks-url: no heartbeat") }
    # The compiler's own version, as UBT checks it: 17.14's folder stays 14.44.35207 (a version the engines
    # ban) however far the compiler inside it has been serviced.
    $msvc = @(Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.VersionInfo.ProductVersion -replace '\.0$', '' })
    if (-not ($msvc | Where-Object { $_ -like '14.44.*' -and [int]($_.Split('.')[2]) -ge 35211 })) { $issues.Add("no usable MSVC 14.44 (>= 14.44.35211): have $($msvc -join ', ')") }
    if (-not (Test-Path 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0')) { $issues.Add('no Windows 11 SDK 10.0.22621') }
    foreach ($i in $issues) { Write-Host "  ! $i" -ForegroundColor Yellow }
    $ok = $issues.Count -eq 0
    Write-Host "  MSVC: $($msvc -join ', ')"
    Write-Host '  The firmware must power the machine on after a power failure (BIOS: "Restore on AC power loss" = Power On);'
    Write-Host '  this script cannot check it.'
    Write-Host '  Running the daily check now (it posts to Slack only if something is wrong):'
    Start-ScheduledTask -TaskPath '\TempoCI\' -TaskName 'Daily'
    $deadline = (Get-Date).AddMinutes(5)
    do { Start-Sleep 3 } while ((Get-ScheduledTask -TaskPath '\TempoCI\' -TaskName 'Daily').State -eq 'Running' -and (Get-Date) -lt $deadline)
    Get-Content "$ciTempo\logs\daily.log" -Tail 15 -ErrorAction SilentlyContinue | ForEach-Object { "    $_" }
    if ($ok) { Write-Host '  OK' -ForegroundColor Green }
}

try {
    switch ($Step) {
        'runners' { Runners }
        'all' { Power-And-Updates; Ci-User; Tools; Ci-Files; Housekeeping; Runners; Checks }
    }
} finally {
    Stop-Transcript | Out-Null
}
