# [co-op join] Two copies of the packaged game on this PC: one hosting the shed, one joined to it.
# Build 169: a friend joined beenster's shed, saw it and couldn't move or do anything, so they never rode
# together. This proves, with nobody at the controls, that a joined player:
#   1. is handed a walker that is theirs to move (coop:check: no menu, not paused, not inside anything),
#   2. walks (coop:walk) and the host sees them walk (coop:seen),
#   3. follows the host onto a Ride UK journey on a bike of their own (coop:ride on the host, coop:onride on the guest).
# Each copy logs "COOP TEST PASS" or "COOP TEST FAILED" lines (Shed/TorqueShedTest.h, coop:*); this script
# waits for both to quit (or kills them after -TimeoutMin) and prints those lines. Exit code 0 only if every
# expected step passed on both copies.
#
#   powershell -ExecutionPolicy Bypass -File Tools\coop_join_test.ps1 -Game <the round's packaged TorqueShedProto.exe>
#
# Run it on the round's staged build, not the play folder (G:\Torque Shed UE): the host plays and saves the
# shed slot in use, and the ride marks its bikes. Both copies run small and capped at 60 fps (they share the
# GPU); nobody touches the keys (no -TorqueNoInput: coop:check would count it as a reason the walker can't
# walk). Test runs never open the router (UPnP). Windows Firewall may ask the first time the game hosts.

param(
	[Parameter(Mandatory = $true)][string]$Game,
	[string]$Ride = "buttertubs",
	[int]$TimeoutMin = 20
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path $Game)) { Write-Error "No game at $Game"; exit 2 }
$Root = Split-Path $Game -Parent
$Logs = Join-Path $Root "TorqueShedProto\Saved\Logs"
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$HostLog = "coop_host.log"
$GuestLog = "coop_guest.log"
Remove-Item -ErrorAction SilentlyContinue (Join-Path $Logs $HostLog), (Join-Path $Logs $GuestLog)

$Common = @("-windowed", "-ResX=1280", "-ResY=720", '-ExecCmds="t.MaxFPS 60"', "-log")
$HostSteps = "coop:players:2:180,coop:check,coop:seen:150,coop:ride:$Ride,wait:10,coop:check,shot,quit"
$GuestSteps = "coop:check,wait:15,coop:walk:300,wait:5,coop:onride:900,wait:5,coop:check,shot,wait:30,quit"

Write-Host "Host: shed, listening on 7777"
$HostArgs = @("/Game/Shed/Shed?listen?play?TorqueName=CoopHost", "-TorqueShedAction=$HostSteps", "-ABSLOG=$(Join-Path $Logs $HostLog)", "-WinX=0", "-WinY=40") + $Common
$HostProc = Start-Process -FilePath $Game -ArgumentList $HostArgs -PassThru
Start-Sleep -Seconds 45   # the shed loaded and hosting before the guest knocks
# (the guest waits 15 s in before walking, so the host has seen where it stood: coop:seen)

Write-Host "Guest: joining 127.0.0.1"
$GuestArgs = @("127.0.0.1?TorqueName=CoopGuest", "-TorqueShedAction=$GuestSteps", "-ABSLOG=$(Join-Path $Logs $GuestLog)", "-WinX=1290", "-WinY=40") + $Common
$GuestProc = Start-Process -FilePath $Game -ArgumentList $GuestArgs -PassThru

$Deadline = (Get-Date).AddMinutes($TimeoutMin)
while ((-not $HostProc.HasExited -or -not $GuestProc.HasExited) -and (Get-Date) -lt $Deadline) { Start-Sleep -Seconds 5 }
foreach ($P in @($HostProc, $GuestProc)) { if (-not $P.HasExited) { Write-Warning "Killing $($P.Id) after $TimeoutMin min"; Stop-Process -Id $P.Id -Force } }

$Ok = $true
function Check([string]$Name, [string]$Log, [string[]]$Expect)
{
	$Path = Join-Path $Logs $Log
	if (-not (Test-Path $Path)) { Write-Host "$Name : NO LOG ($Path)"; $script:Ok = $false; return }
	$Lines = Select-String -Path $Path -Pattern "COOP TEST (PASS|FAILED)|Torque net: joined, |Torque walker: |Torque net: network failure|travel failure" | ForEach-Object { $_.Line }
	Write-Host "== $Name ($Path)"
	$Lines | ForEach-Object { Write-Host "  $_" }
	foreach ($E in $Expect)
	{
		$Hit = $Lines | Where-Object { $_ -match [regex]::Escape($E) -and $_ -match "COOP TEST PASS" }
		if (-not $Hit) { Write-Host "  MISSING PASS: $E"; $script:Ok = $false }
	}
	if ($Lines | Where-Object { $_ -match "COOP TEST FAILED" }) { $script:Ok = $false }
}
Check "Host" $HostLog @("coop:players", "coop:check", "coop:seen", "coop:ride")
Check "Guest" $GuestLog @("coop:check", "coop:walk", "coop:onride")
if ($Ok) { Write-Host "COOP JOIN TEST: PASS"; exit 0 } else { Write-Host "COOP JOIN TEST: FAILED"; exit 1 }
