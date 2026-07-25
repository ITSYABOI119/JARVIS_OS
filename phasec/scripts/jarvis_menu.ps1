<#
.SYNOPSIS
  JARVIS operator menu -- v1. READ-ONLY: it launches Main-PC processes and reads
  box state. Nothing in v1 writes to the box.
  Lives in phasec/scripts alongside start_receiver.ps1 (the Phase-C-era
  access-UX work); every script it drives stays where it always was.

.DESCRIPTION
  Design: phasec/docs/OPERATOR_MENU_DESIGN.md (landed f14e527). This is v1 of
  that design's section 6 scope -- seven options, all safe:

      1  start receiver (send / two-way)      -> start_receiver.ps1
      2  start receiver (display-only)        -> start_receiver.ps1 -DisplayOnly
      3  receiver preflight (-Check)          -> start_receiver.ps1 -Check
      4  box state                            -> ssh probe, then telemetry probe
      5  read telemetry log     (durable NVMe serial log)
      6  read episodic store    (workload memory)
      7  read control-IN store  (real conversation text)
      8  read JACT action audit (every action decision)

  Options 5-8 are one shape: read a NAMED store, never an offset the operator
  typed.

  THE RULES THIS SCRIPT IS BUILT ON (design section 3, and they are load-bearing):

  R1 THIN WRAPPER, NEVER A REIMPLEMENTATION. Every option shells out to the
     existing proven script; this menu owns selection and sequencing, never
     logic. It CALLS start_receiver.ps1 -- it does not replace, absorb or
     duplicate it, so the launcher's UAC-for-send-mode-only, its non-elevated
     browser hand-off and its fail-closed preflight stay in exactly one place
     and the launcher stays independently usable. Enforcement, not just intent:
     -Check prints the exact command string each option runs, and it is the
     SAME string the runner executes (both come from Get-OptionCommand), so a
     divergence is visible in the self-test output.

  R2 READ-ONLY AND DESTRUCTIVE ARE VISUALLY SEPARATE. v1 has no destructive
     options at all, but the section is still rendered -- with what is deferred
     and why -- so the split is established before anything destructive rides
     on it, and so the absence is informative rather than invisible.

  R3 STATE-AWARE. Options that need ssh are shown DISABLED WITH THE REASON when
     the box is not on Ubuntu, never hidden: an option that silently fails
     because the box is in the wrong mode is worse than one that is not
     offered, and hiding it makes the menu look different run-to-run.

  R4 NEVER CLAIM SUCCESS YOU HAVE NOT VERIFIED. Every option reports the real
     exit code of the real command. Where a result is not verified, it says so.

  R5 -Check IS THE CI SUBSTITUTE. A Windows PowerShell script cannot run in
     this project's Linux CI, so there is deliberately NO ci.yml step. -Check
     validates every prerequisite and prints what each option WOULD run without
     running it. Same convention start_receiver.ps1 established.

  THE INVARIANT -- v1 CANNOT WRITE TO THE BOX, AND THAT IS MECHANICALLY
  CHECKABLE. Every store option builds a dd READ operand only. Neither this
  file nor its .bat contains a dd output operand or a seek operand anywhere --
  not in code, not in a comment -- so the property is a grep over these two
  files, not a promise, and a single match is the proof that it broke.

  The exact grep is in CLAUDE.md's Quick Reference row and in
  phasec/docs/OPERATOR_MENU_DESIGN.md. It is deliberately NOT written out
  here: a file that spells its own search pattern always matches it, which
  would make the check useless forever.

  WHY THE dd|python PIPE RUNS ON THE BOX, NOT HERE. The raw sector data never
  crosses a PowerShell pipeline, because PowerShell pipelines carry text and
  would corrupt it -- the same class of bug that silently mangled ~12% of
  generated 32-byte keys until os.O_BINARY was added. Running the documented
  recovery pipe remotely means only TEXT crosses ssh. It also means no
  intermediate file is written anywhere, which is what keeps the invariant
  above true. Residual, stated honestly: this uses the BOX clone's parser, so a
  stale box clone parses with a stale parser. -Check verifies each remote
  parser exists and prints the path it resolved.

  CONSTANTS ARE DERIVED FROM THE HEADERS, NEVER RETYPED. Base LBA and entry
  count come out of nvme_log.h / episodic_store.h / action_audit.h at run time
  (the clear_nvme_log.sh:34-41 precedent), so a menu constant cannot drift from
  the firmware's truth. The operator never types an LBA or a count.

.PARAMETER Check
  Validate every prerequisite, print what each option WOULD run, and exit
  without running anything. 0 = all checks passed.
.PARAMETER AssumeUbuntu
  Treat the box as being on Ubuntu without probing. PER-INVOCATION ONLY --
  there is no config file and no remembered state, deliberately: an override
  that persists is state detection silently disabled forever, long after
  anyone remembers setting it. The override is recorded in the transcript so
  the log shows detection was bypassed.
.PARAMETER AssumeJarvis
  Treat the box as running JARVIS without probing. Same per-invocation rule.
.PARAMETER Port
  The receiver's SSE/HTTP port -- used both for the telemetry state probe and
  passed through to start_receiver.ps1. Default 8800 (the receiver's own
  default).
.PARAMETER KeyFile
  Passed through to start_receiver.ps1 for send mode. This menu never reads,
  prints or transmits the key; it only passes the PATH along. Default:
  %USERPROFILE%\.jarvis\control_key.bin
.PARAMETER Lines
  Store reads: how many trailing lines to show on screen. The FULL output is
  always written to a file and the path printed -- a bare full dump would
  scroll the thing you wanted out of the console buffer. 0 = print everything.
  Default 40.
.PARAMETER BoxHost
  ssh host alias for the box. Default 'jarvis'.
.PARAMETER BoxRepo
  Path to the repo clone ON THE BOX, where the remote parsers live.
  Default '~/Desktop/JARVIS_OS'.
.PARAMETER CaptureInterface
  Optional dumpcap interface for the fallback telemetry probe. Omitted by
  default, which lets dumpcap pick; the probe is best-effort either way.

.NOTES
  Exit codes (this script's own; start_receiver.ps1 owns its codes in its own
  process): 0 ok - 2 ssh not found - 3 a required script is missing -
  4 a header constant could not be derived - 5 both -Assume switches given -
  6 stdin is not an interactive console (use -Check instead).

  Transcript: %USERPROFILE%\.jarvis\menu\transcript.log -- append-only, and it
  NEVER self-rotates or trims, because a tool that edits its own audit log is
  one whose audit log cannot be trusted. It records
  timestamp | option-key | RESOLVED COMMAND | exit code -- the resolved command
  and never the menu label, because "Option 5" is meaningless in six months.
  It never records command OUTPUT and never key material, which matters
  specifically because the control-IN store holds real conversation text.

  Store dumps go to %USERPROFILE%\.jarvis\menu\ -- outside the repo, so
  conversation text cannot be committed by accident.
#>
[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$AssumeUbuntu,
    [switch]$AssumeJarvis,
    [int]$Port = 8800,
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.jarvis\control_key.bin'),
    [int]$Lines = 40,
    [string]$BoxHost = 'jarvis',
    [string]$BoxRepo = '~/Desktop/JARVIS_OS',
    [string]$CaptureInterface = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# This menu lives in <repo>\phasec\scripts; the scripts it drives live in
# <repo>\phasec\scripts and <repo>\phase3\scripts. Derive the repo root from
# our own location so the menu works from any working directory.
$repoRoot     = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launcherPath = Join-Path $PSScriptRoot 'start_receiver.ps1'
$menuHome     = Join-Path $env:USERPROFILE '.jarvis\menu'
$transcript   = Join-Path $menuHome 'transcript.log'

# The box's NVMe device. A constant, not a parameter: v1 only ever reads, and
# a knob here would invite exactly the typo the design set out to remove.
$BoxDevice = '/dev/nvme0n1'

# Relaunch host = the exe running this script (powershell.exe or pwsh).
$psExe = 'powershell.exe'
try { $p = (Get-Process -Id $PID).Path; if ($p) { $psExe = $p } } catch {}

$script:worstExit = 0

function Fail([int]$Code, [string]$Message) {
    Write-Host ("  FAIL: {0}" -f $Message) -ForegroundColor Red
    if (-not $Check) { exit $Code }
    if ($script:worstExit -eq 0) { $script:worstExit = $Code }
}
function Pass([string]$Message) { Write-Host ("  PASS: {0}" -f $Message) -ForegroundColor Green }
function Info([string]$Message) { Write-Host ("  info: {0}" -f $Message) }

if ($AssumeUbuntu -and $AssumeJarvis) {
    Write-Host '  FAIL: -AssumeUbuntu and -AssumeJarvis are mutually exclusive' -ForegroundColor Red
    exit 5
}

# ---------------------------------------------------------------------------
# Constants, DERIVED from the firmware headers (never retyped).
# COUNT = 1 header sector + MAX_ENTRIES entry sectors -- the clear_nvme_log.sh
# convention, and the reason a menu constant cannot drift from the box.
# ---------------------------------------------------------------------------
function Get-HeaderConst {
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) { throw ("header not found: {0}" -f $Path) }
    $pattern = '^\s*#define\s+{0}\s+([0-9]+)' -f [regex]::Escape($Name)
    $m = Select-String -LiteralPath $Path -Pattern $pattern | Select-Object -First 1
    if (-not $m) { throw ("could not derive {0} from {1}" -f $Name, $Path) }
    [uint64]$m.Matches[0].Groups[1].Value
}

$storeDefs = @(
    [pscustomobject]@{ Key='telemetry-log'; Num=5
        Label='telemetry log'
        What='the headless box''s only persistent record -- rolling, keeps the latest'
        Header='phase3\src\drivers\nvme_log.h'
        BaseMacro='NVME_LOG_BASE_LBA'; EntriesMacro='NVME_LOG_MAX_ENTRIES'
        Parser='parse_nvme_log.py'; Sensitive=$false }
    [pscustomobject]@{ Key='episodic'; Num=6
        Label='episodic store'
        What='workload memory; on a busy box this rolls in ~36 s'
        Header='phase3\src\ai\episodic_store.h'
        BaseMacro='EPI_STORE_BASE_LBA'; EntriesMacro='EPI_STORE_MAX_ENTRIES'
        Parser='parse_episodic.py'; Sensitive=$false }
    [pscustomobject]@{ Key='control-in'; Num=7
        Label='control-IN store'
        What='REAL CONVERSATION TEXT -- your queries and the box''s answers'
        Header='phase3\src\ai\episodic_store.h'
        BaseMacro='CTRL_EPI_BASE_LBA'; EntriesMacro='CTRL_EPI_MAX_ENTRIES'
        Parser='parse_episodic.py'; Sensitive=$true }
    [pscustomobject]@{ Key='jact'; Num=8
        Label='JACT action audit'
        What='every action decision: EXECUTED / BLOCKED, with its trigger'
        Header='phase3\src\ai\action_audit.h'
        BaseMacro='ACT_AUDIT_BASE_LBA'; EntriesMacro='ACT_AUDIT_MAX_ENTRIES'
        Parser='parse_action_audit.py'; Sensitive=$false }
)

$stores = @()
foreach ($d in $storeDefs) {
    $hdr = Join-Path $repoRoot $d.Header
    try {
        $base    = Get-HeaderConst -Path $hdr -Name $d.BaseMacro
        $entries = Get-HeaderConst -Path $hdr -Name $d.EntriesMacro
    } catch {
        Fail 4 ("{0}: {1}" -f $d.Key, $_.Exception.Message)
        continue
    }
    $stores += ($d | Select-Object *,
        @{n='Base';    e={$base}},
        @{n='Entries'; e={$entries}},
        @{n='Count';   e={$entries + 1}})
}

# ---------------------------------------------------------------------------
# Resolved commands. ONE source for both -Check ("would run") and the runner
# ("ran") -- R1's enforcement mechanism.
# ---------------------------------------------------------------------------
function Get-StoreCommand {
    param($Store)
    # dd reads (if=) and pipes into the box-side parser; only text crosses ssh.
    # status=none keeps dd's progress off the parsed stream's stderr.
    $remote = 'sudo -n dd if={0} bs=512 skip={1} count={2} status=none | python3 {3}/phase3/scripts/{4}' -f `
              $BoxDevice, $Store.Base, $Store.Count, $BoxRepo, $Store.Parser
    [pscustomobject]@{
        Exe    = 'ssh'
        Args   = @('-o','BatchMode=yes','-o','ConnectTimeout=5',$BoxHost,$remote)
        Pretty = ('ssh -o BatchMode=yes -o ConnectTimeout=5 {0} "{1}"' -f $BoxHost, $remote)
    }
}

function Get-LauncherCommand {
    param([string[]]$Extra, [switch]$NewWindow)
    $a = @('-NoProfile','-ExecutionPolicy','Bypass','-File', $launcherPath) + $Extra
    $pretty = ('{0} -NoProfile -ExecutionPolicy Bypass -File "{1}" {2}' -f `
               (Split-Path -Leaf $psExe), $launcherPath, ($Extra -join ' '))
    if ($NewWindow) { $pretty += '   [in a new window]' }
    [pscustomobject]@{ Exe = $psExe; Args = $a; Pretty = $pretty }
}

function Get-OptionCommand {
    param([int]$Number)
    switch ($Number) {
        1 { Get-LauncherCommand -Extra @('-KeyFile', ('"{0}"' -f $KeyFile), '-Port', "$Port") -NewWindow }
        2 { Get-LauncherCommand -Extra @('-DisplayOnly', '-Port', "$Port") -NewWindow }
        3 { Get-LauncherCommand -Extra @('-Check', '-KeyFile', ('"{0}"' -f $KeyFile), '-Port', "$Port") }
        4 {
            # Not a shell command -- a probe chain. It still belongs in -Check's
            # output (an option that prints nothing reads as an option that does
            # nothing), and the menu reuses this same string for its transcript
            # line so printed and recorded cannot drift.
            [pscustomobject]@{
                Exe    = $null
                Args   = @()
                Pretty = ('probe: ssh {0} -> GET http://127.0.0.1:{1}/events (needs a FRESH recv_ts) -> dumpcap udp/51000' -f $BoxHost, $Port)
            }
        }
        default {
            $s = $stores | Where-Object { $_.Num -eq $Number } | Select-Object -First 1
            if ($s) { Get-StoreCommand -Store $s } else { $null }
        }
    }
}

# ---------------------------------------------------------------------------
# Transcript: append-only, never rotated, never output, never key material.
# ---------------------------------------------------------------------------
function Write-Transcript {
    param([string]$OptionKey, [string]$Command, [string]$ExitCode)
    try {
        if (-not (Test-Path -LiteralPath $menuHome)) {
            New-Item -ItemType Directory -Path $menuHome -Force | Out-Null
        }
        $line = '{0} | {1} | {2} | exit={3}' -f `
                (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK'), $OptionKey, $Command, $ExitCode
        Add-Content -LiteralPath $transcript -Value $line -Encoding UTF8
    } catch {
        Write-Host ("  warn: could not write the transcript ({0})" -f $_.Exception.Message) -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# State detection (R3). ssh -> telemetry(/events, then dumpcap) -> UNKNOWN.
# Never guesses "off": under JARVIS the box answers no ICMP and no ARP, so an
# unreachable box is ambiguous BY CONSTRUCTION.
# ---------------------------------------------------------------------------
function Test-BoxSsh {
    cmd /c ('ssh -o BatchMode=yes -o ConnectTimeout=3 {0} true >nul 2>&1' -f $BoxHost)
    return ($LASTEXITCODE -eq 0)
}

function Test-TelemetrySse {
    # A receiver that has been running since the box was last up replays its
    # cached record to every NEW subscriber (telemetry_receiver.py:1039-1043).
    # So "a record arrived" proves nothing; only a FRESH recv_ts does. Telemetry
    # is ~1 Hz, so anything within MaxAge is live.
    param([int]$TimeoutSec = 4, [double]$MaxAgeSec = 5.0)
    $resp = $null; $sr = $null
    try {
        $req = [System.Net.HttpWebRequest]::Create("http://127.0.0.1:$Port/events")
        $req.Timeout = 2000
        $req.ReadWriteTimeout = $TimeoutSec * 1000
        $resp = $req.GetResponse()
        $sr = New-Object System.IO.StreamReader($resp.GetResponseStream())
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            $line = $sr.ReadLine()
            if ($null -eq $line) { break }
            if (-not $line.StartsWith('data:')) { continue }
            try { $rec = $line.Substring(5).Trim() | ConvertFrom-Json } catch { continue }
            if (-not ($rec.PSObject.Properties.Name -contains 'recv_ts')) { continue }
            $now = [double]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) / 1000.0
            $age = $now - [double]$rec.recv_ts
            if ($age -le $MaxAgeSec) { return $age }
        }
    } catch {
    } finally {
        if ($sr) { $sr.Dispose() }
        if ($resp) { $resp.Dispose() }
    }
    return $null
}

function Get-DumpcapPath {
    $c = Get-Command dumpcap -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $p = Join-Path ${env:ProgramFiles} 'Wireshark\dumpcap.exe'
    if (Test-Path -LiteralPath $p) { return $p }
    return $null
}

function Test-TelemetryDumpcap {
    # Best-effort fallback for a machine with no receiver running. The Main PC
    # cannot simply bind :51000 (Hyper-V excludes it), which is why this is an
    # L2 capture rather than a socket listen.
    $dumpcap = Get-DumpcapPath
    if (-not $dumpcap) { return $null }
    $tmp = Join-Path $env:TEMP ('jarvis-tlm-{0}.pcap' -f ([guid]::NewGuid().ToString('N')))
    $iface = ''
    if ($CaptureInterface) { $iface = ('-i {0} ' -f $CaptureInterface) }
    # -F pcap so an EMPTY capture is exactly the 24-byte classic global header:
    # any larger file means real packets, with no packet counting to get wrong.
    cmd /c ('"{0}" {1}-q -F pcap -f "udp port 51000" -a duration:5 -w "{2}" >nul 2>&1' -f $dumpcap, $iface, $tmp)
    $seen = $false
    try {
        if (Test-Path -LiteralPath $tmp) {
            $seen = ((Get-Item -LiteralPath $tmp).Length -gt 24)
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    } catch {}
    if ($seen) { return $true }
    return $null
}

function Get-BoxState {
    if ($AssumeUbuntu) {
        return [pscustomobject]@{ State='UBUNTU'; Detail='-AssumeUbuntu: detection bypassed for this invocation' }
    }
    if ($AssumeJarvis) {
        return [pscustomobject]@{ State='JARVIS'; Detail='-AssumeJarvis: detection bypassed for this invocation' }
    }
    if (Test-BoxSsh) {
        return [pscustomobject]@{ State='UBUNTU'; Detail='ssh answered' }
    }
    $age = Test-TelemetrySse
    if ($null -ne $age) {
        return [pscustomobject]@{ State='JARVIS'
            Detail=('live telemetry via the receiver on :{0} (record {1:N1}s old)' -f $Port, $age) }
    }
    if ($null -ne (Test-TelemetryDumpcap)) {
        return [pscustomobject]@{ State='JARVIS'; Detail='live telemetry seen on udp/51000 (dumpcap)' }
    }
    return [pscustomobject]@{ State='UNKNOWN'
        Detail='ssh unreachable and no telemetry seen -- the box may be on JARVIS with no receiver running, or off, or unplugged; these are indistinguishable from here' }
}

# ---------------------------------------------------------------------------
# -Check: validate every prerequisite, print what each option WOULD run, exit.
# This is the CI substitute -- no ci.yml step exists or can exist.
# ---------------------------------------------------------------------------
if ($Check) {
    Write-Host '== jarvis_menu.ps1 preflight =='

    if (Get-Command ssh -ErrorAction SilentlyContinue) { Pass 'ssh present on PATH' }
    else { Fail 2 "ssh not found on PATH -- options 5-8 need it (Windows: 'Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0')" }

    if (Test-Path -LiteralPath $launcherPath) { Pass ("launcher present ({0})" -f $launcherPath) }
    else { Fail 3 ("start_receiver.ps1 not found at '{0}' -- options 1-3 call it; this menu deliberately does not duplicate it" -f $launcherPath) }

    foreach ($s in $stores) {
        Pass ('{0}: LBA {1} count {2} (derived from {3}: {4} / {5})' -f `
              $s.Key, $s.Base, $s.Count, (Split-Path -Leaf $s.Header), $s.BaseMacro, $s.EntriesMacro)
    }

    $st = Get-BoxState
    Info ('box state: {0} -- {1}' -f $st.State, $st.Detail)

    if ($st.State -eq 'UBUNTU' -and -not $AssumeUbuntu) {
        foreach ($p in ($stores | Select-Object -ExpandProperty Parser -Unique)) {
            cmd /c ('ssh -o BatchMode=yes -o ConnectTimeout=5 {0} "test -f {1}/phase3/scripts/{2}" >nul 2>&1' -f $BoxHost, $BoxRepo, $p)
            if ($LASTEXITCODE -eq 0) { Pass ('remote parser present ({0}/phase3/scripts/{1})' -f $BoxRepo, $p) }
            else { Fail 3 ('remote parser MISSING: {0}/phase3/scripts/{1} -- the box clone may be elsewhere; pass -BoxRepo' -f $BoxRepo, $p) }
        }
    } else {
        Info 'skip: remote parser checks (they need ssh; box is not confirmed on Ubuntu)'
    }

    $dc = Get-DumpcapPath
    if ($dc) { Info ("dumpcap present ({0}) -- the fallback telemetry probe is available" -f $dc) }
    else { Info 'dumpcap NOT found -- the fallback telemetry probe is unavailable; with no receiver running the state reads UNKNOWN (not an error)' }

    try {
        if (-not (Test-Path -LiteralPath $menuHome)) { New-Item -ItemType Directory -Path $menuHome -Force | Out-Null }
        Pass ("transcript directory writable ({0})" -f $menuHome)
    } catch { Fail 3 ("cannot create '{0}' -- the transcript and store dumps live there" -f $menuHome) }

    Write-Host ''
    Write-Host '-- what each option WOULD run --'
    foreach ($n in 1..8) {
        $c = Get-OptionCommand -Number $n
        if ($c) { Write-Host ("  {0}) {1}" -f $n, $c.Pretty) }
    }
    Write-Host ''
    Write-Host '  note: the receiver''s own preflight is option 3 (start_receiver.ps1 -Check); this'
    Write-Host '        menu does not duplicate its py/key/Npcap checks.'
    Write-Host ''
    if ($script:worstExit -eq 0) { Write-Host '== preflight PASS ==' }
    else { Write-Host ("== preflight FAIL (exit {0}) ==" -f $script:worstExit) }
    exit $script:worstExit
}

# ---------------------------------------------------------------------------
# Runners
# ---------------------------------------------------------------------------
function Invoke-StoreRead {
    param($Store)
    $cmd = Get-StoreCommand -Store $Store
    Write-Host ''
    Write-Host ('== read {0} ==' -f $Store.Label)
    Write-Host ('  LBA {0}, {1} sectors (1 header + {2} entries), derived from {3}' -f `
                $Store.Base, $Store.Count, $Store.Entries, (Split-Path -Leaf $Store.Header))
    Write-Host ('  {0}' -f $cmd.Pretty)
    if ($Store.Sensitive) {
        Write-Host '  note: this store holds real conversation text; the dump is written outside the repo' -ForegroundColor Yellow
    }
    Write-Host ''

    $out = & $cmd.Exe @($cmd.Args)
    $rc = $LASTEXITCODE
    Write-Transcript -OptionKey ('read-{0}' -f $Store.Key) -Command $cmd.Pretty -ExitCode $rc

    if ($rc -ne 0) {
        # sudo -n fails FAST and legibly instead of hanging on a non-tty, which
        # is the whole reason it is -n (design T4).
        Write-Host ("  the read FAILED (exit {0}) -- nothing was parsed" -f $rc) -ForegroundColor Red
        Write-Host '  if this says "sudo: a password is required", passwordless sudo on the box is gone' -ForegroundColor Red
        return
    }

    # NOT named $lines: PowerShell variable names are CASE-INSENSITIVE, so a
    # local $lines silently overwrites the $Lines parameter, and the tail test
    # below then compares the array's count against itself -- always true, so
    # the limit never applies and the whole dump lands in the console. That is
    # precisely the failure this feature exists to prevent, and it does not
    # show up in -Check because -Check never reads a store.
    $outLines = @($out)
    if (-not (Test-Path -LiteralPath $menuHome)) { New-Item -ItemType Directory -Path $menuHome -Force | Out-Null }
    $dump = Join-Path $menuHome ('{0}-{1}.txt' -f $Store.Key, (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Set-Content -LiteralPath $dump -Value $outLines -Encoding UTF8

    if ($Lines -le 0 -or $outLines.Count -le $Lines) {
        $outLines | ForEach-Object { Write-Host $_ }
    } else {
        Write-Host ('  ... showing the last {0} of {1} lines ...' -f $Lines, $outLines.Count) -ForegroundColor DarkGray
        $outLines | Select-Object -Last $Lines | ForEach-Object { Write-Host $_ }
    }
    Write-Host ''
    Write-Host ('  full output ({0} lines): {1}' -f $outLines.Count, $dump) -ForegroundColor Cyan
}

function Invoke-Launcher {
    param([int]$Number, [string]$Key, [switch]$NewWindow)
    $cmd = Get-OptionCommand -Number $Number
    Write-Host ''
    Write-Host ('  {0}' -f $cmd.Pretty)
    Write-Host ''
    if ($NewWindow) {
        # Its own window, deliberately: the receiver's [send]/[reply] log lines
        # matter and Ctrl+C must stop the RECEIVER, not this menu. In send mode
        # start_receiver.ps1 self-elevates from there -- this menu never
        # reimplements that (R1).
        Start-Process -FilePath $cmd.Exe -ArgumentList $cmd.Args | Out-Null
        Write-Transcript -OptionKey $Key -Command $cmd.Pretty -ExitCode 'launched'
        Write-Host '  launched in a new window. This menu stays usable.' -ForegroundColor Green
        Write-Host '  (send mode prompts for UAC there -- the raw L2 send path needs it)'
        Write-Host '  RAN, UNVERIFIED: the launcher owns its own preflight; watch that window.' -ForegroundColor DarkGray
    } else {
        & $cmd.Exe @($cmd.Args)
        $rc = $LASTEXITCODE
        Write-Transcript -OptionKey $Key -Command $cmd.Pretty -ExitCode $rc
        Write-Host ''
        if ($rc -eq 0) { Write-Host '  preflight PASS (exit 0)' -ForegroundColor Green }
        else { Write-Host ("  preflight FAIL (exit {0}) -- see the launcher's own exit-code table" -f $rc) -ForegroundColor Red }
    }
}

# ---------------------------------------------------------------------------
# Menu
# ---------------------------------------------------------------------------
# The menu is interactive BY CONSTRUCTION. With stdin redirected (a pipe, a
# scheduler, a CI runner) Read-Host does not block -- it returns empty
# immediately -- and the loop below would spin forever burning CPU. Measured
# before this guard existed: ~10,000 iterations and a 232,569-line log in three
# minutes. Fail fast and legibly instead, and name the entry point that IS
# non-interactive.
if ([Console]::IsInputRedirected) {
    Write-Host 'jarvis_menu.ps1 is interactive: run it in a console, or double-click jarvis_menu.bat.'
    Write-Host 'stdin is redirected here, so nothing can answer the prompt.'
    Write-Host 'For a non-interactive run use -Check -- it validates every prerequisite and prints'
    Write-Host 'what each option would run, without running anything.'
    exit 6
}

if ($AssumeUbuntu -or $AssumeJarvis) {
    $which = if ($AssumeUbuntu) { '-AssumeUbuntu' } else { '-AssumeJarvis' }
    Write-Transcript -OptionKey 'assume-override' -Command ('{0} (state detection bypassed for this invocation)' -f $which) -ExitCode '-'
}

$state = Get-BoxState
$emptyReads = 0

while ($true) {
    $sshOk = ($state.State -eq 'UBUNTU')
    $color = switch ($state.State) { 'UBUNTU' { 'Green' } 'JARVIS' { 'Cyan' } default { 'Yellow' } }

    Write-Host ''
    Write-Host '==================== JARVIS operator menu (v1, read-only) ===================='
    Write-Host -NoNewline '  box state: '
    Write-Host ('{0}' -f $state.State) -ForegroundColor $color -NoNewline
    Write-Host ('  --  {0}' -f $state.Detail)
    Write-Host ''
    Write-Host '  -- Main PC (safe; valid in any box state) --'
    Write-Host '   1) start receiver -- send / two-way console   (UAC prompt; new window)'
    Write-Host '   2) start receiver -- display-only             (no key, no elevation)'
    Write-Host '   3) receiver preflight (-Check)'
    Write-Host '   4) re-check box state'
    Write-Host ''
    Write-Host '  -- Box reads (safe; read-only, need ssh) --'
    foreach ($s in $stores) {
        $line = '   {0}) read {1,-18} -- {2}' -f $s.Num, $s.Label, $s.What
        if ($sshOk) {
            Write-Host $line
        } else {
            # Shown DISABLED with the reason, never hidden (R3).
            Write-Host ($line + '   [unavailable]') -ForegroundColor DarkGray
        }
    }
    if (-not $sshOk) {
        Write-Host ''
        Write-Host ('   ^ unavailable: ssh needs the box on Ubuntu (state is {0}).' -f $state.State) -ForegroundColor DarkGray
        Write-Host '     seL4 has no shell and no sshd, so this is not a bug to work around.' -ForegroundColor DarkGray
        Write-Host '     If you know better than the probe, re-run with -AssumeUbuntu.' -ForegroundColor DarkGray
    }
    Write-Host ''
    Write-Host '  -- Changes the box --' -ForegroundColor DarkGray
    Write-Host '     (nothing in v1: deploy, key provision, boot-into-JARVIS and clear-log are' -ForegroundColor DarkGray
    Write-Host '      deferred -- each needs its own typed confirmation and read-back proof;' -ForegroundColor DarkGray
    Write-Host '      build and KVM smoke need streamed output and a cancel story; the semantic' -ForegroundColor DarkGray
    Write-Host '      store has no parser yet.)' -ForegroundColor DarkGray
    Write-Host ''
    Write-Host '   q) quit'
    Write-Host '============================================================================='

    $choice = Read-Host 'choose'
    $choice = "$choice".Trim().ToLower()
    if ($choice -eq 'q' -or $choice -eq 'quit') { break }
    if ($choice -eq '') {
        # A console driven to EOF (Ctrl+Z) also returns empty forever, and
        # IsInputRedirected is FALSE for that case -- so the guard above does
        # not cover it and this one does.
        $emptyReads++
        if ($emptyReads -ge 3) {
            Write-Host '  empty input three times running (stdin at EOF?) -- exiting rather than spinning.'
            break
        }
        continue
    }
    $emptyReads = 0

    try {
        switch ($choice) {
            '1' { Invoke-Launcher -Number 1 -Key 'receiver-send' -NewWindow }
            '2' { Invoke-Launcher -Number 2 -Key 'receiver-display-only' -NewWindow }
            '3' { Invoke-Launcher -Number 3 -Key 'receiver-check' }
            '4' {
                Write-Host ''
                Write-Host '  probing...'
                $state = Get-BoxState
                Write-Transcript -OptionKey 'box-state' -Command (Get-OptionCommand -Number 4).Pretty -ExitCode $state.State
                Write-Host ('  box state: {0} -- {1}' -f $state.State, $state.Detail)
            }
            default {
                $s = $stores | Where-Object { "$($_.Num)" -eq $choice } | Select-Object -First 1
                if (-not $s) {
                    Write-Host ('  not an option: "{0}"' -f $choice) -ForegroundColor Yellow
                } elseif (-not $sshOk) {
                    Write-Host ('  unavailable while the box state is {0} -- ssh cannot reach seL4.' -f $state.State) -ForegroundColor Yellow
                    Write-Host '  Re-run the menu with -AssumeUbuntu to override the probe.' -ForegroundColor Yellow
                } else {
                    Invoke-StoreRead -Store $s
                }
            }
        }
    } catch {
        # One bad option must not drop the operator out of the menu.
        Write-Host ('  ERROR: {0}' -f $_.Exception.Message) -ForegroundColor Red
    }
}

Write-Host ''
Write-Host ('transcript: {0}' -f $transcript)
exit 0
