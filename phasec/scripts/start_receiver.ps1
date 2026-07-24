<#
.SYNOPSIS
  One-click launcher for the JARVIS Remote Telemetry Console receiver
  (phase3/scripts/telemetry_receiver.py) on the Main PC.
  Lives in phasec/scripts (the Phase-C-era access-UX work); the receiver it
  launches stays where it always was, phase3/scripts.

.DESCRIPTION
  Replaces the hand-run "open an Administrator PowerShell, remember the flags"
  ritual for the two-way control-IN console (Phase 6 goal 6-5). It only
  LAUNCHES the receiver as-is: it embeds no key material (the -KeyFile path
  points at the 32-byte JKEY the operator provisioned out-of-band) and weakens
  none of the receiver's own guards (loopback pinning, fail-closed key load,
  CSRF/origin checks all live in telemetry_receiver.py, untouched).

  Default (send mode):
      py -3 <repo>\phase3\scripts\telemetry_receiver.py --sse --http-port <Port> --send --key-file <KeyFile> [--box-mac ..] [--box-ip ..]
    then opens the default browser at the loopback console URL the receiver
    serves (http://127.0.0.1:<Port>/) once the HTTP port answers.
    --send transmits a raw L2 Ethernet frame (scapy + Npcap), which needs
    ELEVATION, so the launcher self-elevates via UAC when not already
    running as Administrator.

  -DisplayOnly:
      py -3 ...\telemetry_receiver.py --sse --http-port <Port>
    Telemetry display only — no --send, no key, no Npcap, and therefore NO
    elevation (the launcher does not self-elevate in this mode).

  -Check: run every preflight check, print PASS/FAIL + the exact command a
    real launch would run, and exit (0 = all pass). This IS the launcher's
    fail-loud self-test — a Windows PowerShell script cannot run in the
    project's Linux CI, so there is deliberately no ci.yml step for it.

  FAIL-CLOSED preflight (each failure prints a clear message and exits
  non-zero; nothing is launched):
    - the 'py' launcher with a Python 3 runtime is present
    - telemetry_receiver.py exists next to this script
    - (send mode) the key file exists and is EXACTLY 32 bytes
    - (send mode) Npcap is present (the raw L2 path needs it)

  Box MAC/IP: left to the receiver's own provisioned defaults unless
  -BoxMac / -BoxIp are passed (the launcher never duplicates them here).

.PARAMETER KeyFile
  Path to the 32-byte raw HMAC-SHA256 key (the box's JKEY slot), provisioned
  out-of-band. Default: %USERPROFILE%\.jarvis\control_key.bin
.PARAMETER BoxMac
  Override the receiver's provisioned box MAC (passed through as --box-mac).
.PARAMETER BoxIp
  Override the receiver's provisioned box IP (passed through as --box-ip).
.PARAMETER Port
  HTTP/SSE port the receiver serves the console on (passed through as
  --http-port). Default: 8800.
.PARAMETER DisplayOnly
  Telemetry display only: drop --send/--key-file, skip the key + Npcap
  checks, do not elevate.
.PARAMETER Check
  Validate the environment and exit without launching anything.
.PARAMETER Elevated
  INTERNAL — set by the UAC self-relaunch so the elevated window pauses on
  failure instead of closing before the message can be read. Do not pass by
  hand.

.NOTES
  Exit codes: 0 ok · 2 py/Python3 missing · 3 receiver script missing ·
  4 key file missing or not exactly 32 bytes · 5 Npcap missing ·
  6 UAC elevation declined/failed.
  Known gotcha: selecting text in the receiver's console window freezes it
  (QuickEdit) while telemetry keeps flowing — press Esc to resume.
#>
[CmdletBinding()]
param(
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.jarvis\control_key.bin'),
    [string]$BoxMac = '',
    [string]$BoxIp = '',
    [int]$Port = 8800,
    [switch]$DisplayOnly,
    [switch]$Check,
    [switch]$Elevated
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# This launcher lives in <repo>\phasec\scripts; the receiver lives in
# <repo>\phase3\scripts. Derive the repo root from our own location so the
# launcher works from any working directory.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$receiverPath = Join-Path $repoRoot 'phase3\scripts\telemetry_receiver.py'
$consoleUrl = "http://127.0.0.1:$Port/"
# Relaunch/helper host = the exe running this script (powershell.exe or pwsh).
$psExe = 'powershell.exe'
try { $p = (Get-Process -Id $PID).Path; if ($p) { $psExe = $p } } catch {}

function Test-IsElevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Wait-BeforeClose {
    # A UAC-spawned window closes the instant the script exits — pause so the
    # operator can actually read a failure.
    if ($Elevated) { Write-Host ''; Read-Host 'press Enter to close this window' | Out-Null }
}

function Fail([int]$Code, [string]$Message) {
    Write-Host ("  FAIL: {0}" -f $Message) -ForegroundColor Red
    if (-not $Check) { Wait-BeforeClose; exit $Code }
    $script:worstExit = if ($script:worstExit -eq 0) { $Code } else { $script:worstExit }
}

function Pass([string]$Message) {
    Write-Host ("  PASS: {0}" -f $Message) -ForegroundColor Green
}

$script:worstExit = 0

# ---------------------------------------------------------------------------
# Preflight (fail-closed). In -Check mode all checks run and report; otherwise
# the first failure exits immediately with its code.
# ---------------------------------------------------------------------------
Write-Host '== start_receiver.ps1 preflight =='

# 1) py launcher + a Python 3 runtime (the receiver is py -3 by project convention)
$pyCmd = Get-Command py -ErrorAction SilentlyContinue
if (-not $pyCmd) {
    Fail 2 "the 'py' launcher was not found on PATH -- install Python 3 from python.org (the launcher ships with it)"
} else {
    cmd /c "py -3 --version >nul 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Fail 2 "'py -3' has no Python 3 runtime -- install Python 3 from python.org"
    } else {
        $pyVer = (cmd /c "py -3 --version 2>&1" | Select-Object -First 1)
        Pass ("py launcher present ({0})" -f $pyVer)
    }
}

# 2) the receiver script itself
if (-not (Test-Path -LiteralPath $receiverPath)) {
    Fail 3 ("telemetry_receiver.py not found at '{0}' -- this launcher expects to live at <repo>\phasec\scripts with the receiver at <repo>\phase3\scripts" -f $receiverPath)
} else {
    Pass ("receiver script present ({0})" -f $receiverPath)
}

# 3) + 4) send-mode-only prerequisites (the display half needs neither)
if (-not $DisplayOnly) {
    if (-not (Test-Path -LiteralPath $KeyFile)) {
        Fail 4 ("key file '{0}' not found -- provision the box's 32-byte JKEY out-of-band (this launcher never embeds a key), or run with -DisplayOnly for telemetry-only" -f $KeyFile)
    } else {
        $keyLen = (Get-Item -LiteralPath $KeyFile).Length
        if ($keyLen -ne 32) {
            Fail 4 ("key file '{0}' is {1} bytes, expected EXACTLY 32 (the raw HMAC-SHA256 key, not hex/base64)" -f $KeyFile, $keyLen)
        } else {
            Pass ("key file present, exactly 32 bytes ({0})" -f $KeyFile)
        }
    }

    $npcapSvc = Get-Service -Name npcap -ErrorAction SilentlyContinue
    $npcapDll = Join-Path $env:WINDIR 'System32\Npcap\wpcap.dll'
    if (-not $npcapSvc -and -not (Test-Path -LiteralPath $npcapDll)) {
        Fail 5 "Npcap not detected (no 'npcap' service and no System32\Npcap\wpcap.dll) -- install it from https://npcap.com (the raw L2 send path needs it); -DisplayOnly runs without it"
    } else {
        $src = if ($npcapSvc) { "service 'npcap'" } else { $npcapDll }
        Pass ("Npcap present ({0})" -f $src)
    }
} else {
    Write-Host '  skip: key file + Npcap checks (-DisplayOnly needs neither)'
}

# The exact receiver invocation (built once; -Check prints it, a launch runs it).
$recvArgs = @('--sse', '--http-port', "$Port")
if (-not $DisplayOnly) { $recvArgs += @('--send', '--key-file', $KeyFile) }
if ($BoxMac) { $recvArgs += @('--box-mac', $BoxMac) }
if ($BoxIp) { $recvArgs += @('--box-ip', $BoxIp) }

if ($Check) {
    $elevNote = if (Test-IsElevated) { 'elevated (Administrator)' }
                elseif ($DisplayOnly) { 'not elevated (fine: -DisplayOnly never elevates)' }
                else { 'not elevated (a real launch will self-elevate via UAC)' }
    Write-Host ("  info: elevation: {0}" -f $elevNote)
    Write-Host ("  would run: py -3 `"{0}`" {1}" -f $receiverPath, ($recvArgs -join ' '))
    Write-Host ("  console URL: {0}" -f $consoleUrl)
    if ($script:worstExit -eq 0) {
        Write-Host '== preflight PASS =='
    } else {
        Write-Host ("== preflight FAIL (exit {0}) ==" -f $script:worstExit)
    }
    exit $script:worstExit
}

# ---------------------------------------------------------------------------
# Elevation: only the raw L2 send path needs Administrator. Re-launch this
# same script (same parameters + -Elevated) via UAC and hand off.
# ---------------------------------------------------------------------------
if (-not $DisplayOnly -and -not (Test-IsElevated)) {
    Write-Host '  info: --send needs a raw L2 socket -> relaunching elevated (UAC prompt)...'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass',
                 '-File', ('"{0}"' -f $PSCommandPath),
                 '-KeyFile', ('"{0}"' -f $KeyFile),
                 '-Port', $Port)
    if ($BoxMac) { $argList += @('-BoxMac', $BoxMac) }
    if ($BoxIp) { $argList += @('-BoxIp', $BoxIp) }
    $argList += '-Elevated'
    try {
        Start-Process -FilePath $psExe -ArgumentList $argList -Verb RunAs | Out-Null
    } catch {
        Write-Host '  FAIL: elevation was declined or failed -- --send cannot run without it (use -DisplayOnly for telemetry-only)' -ForegroundColor Red
        exit 6
    }
    Write-Host '  info: elevated window launched; the receiver runs there. This window is done.'
    exit 0
}

# ---------------------------------------------------------------------------
# Launch: open the browser once the console URL answers, then run the
# receiver IN THIS WINDOW (its [send]/[reply] log lines matter; Ctrl+C stops).
# ---------------------------------------------------------------------------
$mode = if ($DisplayOnly) { 'display-only (no send path)' } else { 'two-way (--send; signing enabled)' }
Write-Host ''
Write-Host '== launching the JARVIS telemetry receiver =='
Write-Host ("  mode:    {0}" -f $mode)
if (-not $DisplayOnly) { Write-Host ("  key:     {0} (32 bytes; never printed)" -f $KeyFile) }
if ($BoxMac) { Write-Host ("  box MAC: {0} (override)" -f $BoxMac) }
if ($BoxIp) { Write-Host ("  box IP:  {0} (override)" -f $BoxIp) }
Write-Host ("  console: {0} (opens in your browser when the port answers)" -f $consoleUrl)
Write-Host '  stop:    Ctrl+C in this window'
Write-Host '  gotcha:  selecting text in this window pauses the receiver (QuickEdit) -- press Esc to resume'
Write-Host ''

# Detached helper: poll the HTTP port (up to ~30 s), then open the console URL.
# The open goes through explorer.exe DELIBERATELY: in send mode this whole
# script (and therefore this helper) runs ELEVATED, and a plain
# Start-Process '<url>' from an elevated context starts the default browser
# as Administrator whenever no browser is already running — a sandbox-hygiene
# regression (adversarial-review finding). explorer.exe brokers the open
# through the operator's normal-integrity shell, so the browser never
# inherits the Administrator token. Detached so the receiver owns this console.
$opener = @"
for (`$i = 0; `$i -lt 60; `$i++) {
    try {
        (New-Object Net.Sockets.TcpClient('127.0.0.1', $Port)).Close()
        explorer.exe '$consoleUrl'
        break
    } catch { Start-Sleep -Milliseconds 500 }
}
"@
$openerEnc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($opener))
Start-Process -FilePath $psExe -WindowStyle Hidden -ArgumentList @(
    '-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-EncodedCommand', $openerEnc) | Out-Null

& py -3 $receiverPath @recvArgs
$recvExit = $LASTEXITCODE

if ($recvExit -ne 0) {
    Write-Host ("receiver exited with code {0}" -f $recvExit) -ForegroundColor Red
    Wait-BeforeClose
}
exit $recvExit
