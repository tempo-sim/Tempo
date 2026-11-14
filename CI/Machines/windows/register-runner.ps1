# register-runner.ps1 VERSION[,VERSION...]: register this machine's runner for each Unreal version with the
# tempo-sim org, in runner group tempo-ci, as a Windows service running as the CI user, and start it. Run it
# elevated, after bootstrap.ps1:
#
#   powershell -ExecutionPolicy Bypass -File CI\Machines\windows\register-runner.ps1 5.7,5.8
#
# It asks for two things, in this window, and keeps neither:
# - a registration token: org Settings > Actions > Runners > New runner shows one, or
#     gh api -X POST orgs/tempo-sim/actions/runners/registration-token -q .token
#   It expires after an hour; one token registers every version.
# - the CI user's password, which the service logs on with.
#
# One runner per Unreal version, because a job's build tree is fixed per version and not locked: the native
# workflow adds tempo-ue-<version> to every job's labels, so only this runner takes that version's jobs.
# Re-running for a version replaces its registration.
#Requires -RunAsAdministrator
[CmdletBinding()]
param([Parameter(Mandatory = $true)] [string[]] $Versions)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Here = $PSScriptRoot
$CiUser = if ($env:TEMPO_CI_USER) { $env:TEMPO_CI_USER } else { 'ci' }
$Org = if ($env:TEMPO_CI_ORG) { $env:TEMPO_CI_ORG } else { 'tempo-sim' }
$Group = if ($env:TEMPO_CI_RUNNER_GROUP) { $env:TEMPO_CI_RUNNER_GROUP } else { 'tempo-ci' }

$sid = (New-Object Security.Principal.NTAccount($CiUser)).Translate([Security.Principal.SecurityIdentifier]).Value
$ciHome = (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\ProfileList\$sid").ProfileImagePath
$machineName = $null
if (Test-Path "$ciHome\tempo-ci\machine.env") {
    $m = Select-String -Path "$ciHome\tempo-ci\machine.env" -Pattern '^TEMPO_CI_MACHINE_NAME=(.+)$' | Select-Object -First 1
    if ($m) { $machineName = $m.Matches[0].Groups[1].Value.Trim('"', "'", ' ') }
}
if (-not $machineName) { $machineName = $env:COMPUTERNAME.ToLower() }

function Plain($secure) {
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}
$token = Plain (Read-Host 'Runner registration token' -AsSecureString)
$password = Plain (Read-Host "Password of $CiUser (the runner services log on with it)" -AsSecureString)
if (-not $token -or -not $password) { throw 'Both are needed.' }

$release = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/actions/runner/releases/latest'
$tag = $release.tag_name
try {
    foreach ($version in $Versions) {
        $name = "$machineName-$version"
        $dir = "$ciHome\actions-runner-$version"
        Write-Host "`n== $name ($dir)" -ForegroundColor Cyan
        if (-not (Test-Path "$dir\config.cmd")) {
            Write-Host "Installing runner $tag"
            New-Item -ItemType Directory -Force $dir | Out-Null
            $zip = Join-Path $env:TEMP "actions-runner-$($tag.TrimStart('v')).zip"
            if (-not (Test-Path $zip)) {
                Invoke-WebRequest -UseBasicParsing -OutFile $zip `
                    "https://github.com/actions/runner/releases/download/$tag/actions-runner-win-x64-$($tag.TrimStart('v')).zip"
            }
            Expand-Archive -Path $zip -DestinationPath $dir -Force
        }
        Push-Location $dir
        try {
            # config.cmd refuses to configure a runner directory twice: remove the old registration (and its
            # service) first.
            if (Test-Path "$dir\.runner") {
                & .\config.cmd remove --token $token
                if ($LASTEXITCODE -ne 0) { throw "config.cmd remove failed for $name." }
            }
            & .\config.cmd --unattended --replace --url "https://github.com/$Org" --token $token `
                --name $name --runnergroup $Group --labels "tempo-ci,tempo-ue-$version" --work _work `
                --runasservice --windowslogonaccount "$env:COMPUTERNAME\$CiUser" --windowslogonpassword $password
            if ($LASTEXITCODE -ne 0) { throw "config.cmd failed for $name." }
        } finally {
            Pop-Location
        }
    }
} finally {
    $token = $null; $password = $null
}
# config.cmd leaves its caller's PATH behind; bootstrap writes the jobs' PATH and environment.
& "$Here\bootstrap.ps1" runners
