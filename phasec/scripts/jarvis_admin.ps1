<#
.SYNOPSIS
  JARVIS admin -- the DESTRUCTIVE half of the operator tooling. v2 scope: the
  control-IN re-key. Separate from jarvis_menu.ps1 on purpose; see below.

.DESCRIPTION
  Encodes the control-IN re-key performed BY HAND on 2026-07-26 (box boot_id=44,
  answered=1 blocked=0 dropped=0 err=0). That manual run surfaced roughly ten
  checks the runbook does not have, and one place where the runbook is wrong.
  Those checks are the reason this exists: they are exactly the ones a human
  skips on the fifth repetition.

  WHY THIS IS A SEPARATE FILE AND NOT A MENU OPTION -- the load-bearing reason.

  jarvis_menu.ps1's safety property is not a promise, it is a grep: neither the
  menu nor its .bat contains a dd OUTPUT operand or a SEEK operand anywhere, so
  "v1 cannot write to the box" is mechanically checkable and a single match is
  the proof that it broke. Re-keying needs both of those operands by definition.
  Putting it in the menu would have traded a checkable invariant for a claim.

  So the invariant is now TWO-SIDED, and says more than it did before: device
  writes live in exactly one file, and that file's name says so. The exact greps
  live in CLAUDE.md's Quick Reference row and in
  phasec/docs/OPERATOR_MENU_DESIGN.md -- deliberately NOT written out here,
  because a file that spells its own search pattern always matches it, which
  would make the check useless forever. (Same reasoning as the menu's.)

  MODES -- there is no default. A destructive tool must do nothing on a bare
  invocation, so no arguments prints usage and exits non-zero.

    -Check      validate every prerequisite and print what every step WOULD run.
                Runs no write, no generation, no transfer. Safe at any time.
    -Rekey      the full procedure, behind a typed confirmation before the write.
    -Rollback   restore both halves from the .BAK pair, then verify.

  -Check IS THE ANTI-ROT MECHANISM, not merely the CI substitute. Re-keying is
  INCIDENT RESPONSE, not scheduled rotation (gen_control_key.py's docstring is
  emphatic, and the reasons are good: the box has no RTC, the key never touches
  the wire, and an automated key-DELIVERY channel would be a higher-value target
  than the static secret it replaces). A script that runs twice a year decays
  silently and then fails on the day it is actually needed. -Check exercises the
  real prerequisites -- it really does read the box slot and really does compare
  fingerprints -- so it cannot pass vacuously the way a check that touches
  nothing can. (The menu shipped a broken -Lines for exactly that reason: its
  -Check never read a store.)

  WHAT MAKES THIS SAFE, HONESTLY. Wrapping a destructive command in a script
  does not make it less destructive; it makes it easier to reach. What makes it
  safe is that every gate below is a HARD ABORT rather than a warning, that the
  write sits behind a typed confirmation, and that nothing live is touched until
  the new pair is fully verified in a scratch directory.

  THE GATES (each aborts with its own exit code; see .NOTES)

    P1  ssh reaches the box  -- it MUST be on Ubuntu. If ssh times out the box
        is on JARVIS (seL4 has no sshd) or off; never re-key a running box.
    P2  box slot reads magic JKEY with a non-zero key      -> FP_BOX
    P3  PC key file is exactly 32 bytes                    -> FP_PC
    P4  FP_BOX == FP_PC. The check the runbook lacks. If they differ the channel
        is ALREADY broken and re-keying is the wrong action -- use -Rollback.
    P5  no receiver answering on the console port. A receiver caches the key at
        startup, so one left running signs with the OLD key afterwards.
    B1  PC backup verified by SIZE **and FINGERPRINT**
    B2  box backup verified by SIZE **and FINGERPRINT**
        -- fingerprint, because a 512-byte file of zeros passes a size check.
           This is what makes the write safe to take.
    G1  generate into a SCRATCH dir, never over the live paths, never --force
        (scratch keeps gen_control_key.py's own fail-closed guard armed)
    G2  new key file 32 bytes, non-zero                    -> FP_NEW
    G3  new slot 512 B, magic, version, reserved0/seq_floor/boot_epoch zero,
        bytes 52..512 zero
    G4  the key INSIDE the slot equals the key file. Highest-value check here:
        a mismatched pair provisions "successfully" and the channel is dead with
        no error anywhere until a query gets no reply.
    G5  FP_NEW != FP_BOX
    T1  scp the slot to the box
    T2  whole-slot md5 identical on both sides -- not "512 bytes arrived". This
        project has silently corrupted binary through a text channel three times
        (seL4 CR/LF, Python text mode, PowerShell pipeline), and
        gen_control_key.py carries an O_BINARY fix for exactly that class.
    --  typed confirmation, then the dd write
    W1  read back FROM THE DEVICE (not from the file); fingerprint == FP_NEW
    W2  whole-sector md5 == the local slot's md5
    W3  NEIGHBOURS INTACT: LBA+1/+2 = JFLR, LBA+3 = JCON. Catches the failure
        that would not show up in the target sector at all -- one wrong digit in
        seek= lands on the replay floor, the console address, the JACT audit
        store or the control-IN conversation store. That is a data-loss event,
        not a re-key failure, and the operator needs to know immediately.
    S1  copy the scratch key over the live key file
    S2  live key fingerprint == FP_NEW == the on-device fingerprint

  THE LBA IS PARSED FROM phase3/src/net/control_key.h (CTRL_KEY_BASE_LBA), never
  hardcoded -- the gen_control_key.py and jarvis_menu.ps1 precedent. A constant
  typed into a script can drift from the firmware; a parsed one cannot. The
  neighbour magics and their offsets are parsed the same way, out of
  control_floor.h and control_console.h.

  KEY MATERIAL IS NEVER PRINTED, LOGGED OR ECHOED. Every identity claim in the
  output is a FINGERPRINT = sha256(key_bytes) truncated to 8 hex characters --
  32 bits of a hash, not reversible -- so the transcript is safe to paste into a
  bug report.

  CLEANUP IS A GATE, NOT A COURTESY (and it runs only after S2 passes, so an
  aborted run never destroys its own rollback path). The manual run left six
  stray artifacts. The one that mattered was neither old key: it was the scp'd
  scratch slot sitting in the BOX's home directory -- a plaintext copy of the
  CURRENTLY LIVE key on a dual-booting LAN machine. The key is supposed to exist
  in exactly two places: the JKEY sector and the Main PC key file. Old-key
  artifacts are inert by comparison, because the box no longer accepts that key
  and a frame signed with it is dropped at HMAC.

  Note plainly: rm UNLINKS, it does not securely erase, and on an SSD
  overwriting would not guarantee erasure either. No shred is implied.

.PARAMETER Check
  Validate everything, print what each step would run, change nothing. Exit 0
  means a -Rekey could proceed right now.
.PARAMETER Rekey
  Run the full procedure. Prompts for a typed confirmation before the write.
.PARAMETER Rollback
  Restore the previous key pair from the .BAK artifacts and verify. Intended for
  an ABORTED run (whose backups are still in place) or a re-key that verified but
  behaved badly. After a successful -Rekey the backups are gone by default -- see
  -KeepBackups.
.PARAMETER KeepBackups
  Retain the old key pair after a successful re-key. Default is DELETE, because
  the rollback purpose is discharged the moment S2 passes, and the box's own JKEY
  sector is itself a recovery path for the PC half.
.PARAMETER KeyFile
  The live 32-byte key on this PC. Default %USERPROFILE%\.jarvis\control_key.bin
  (the start_receiver.ps1 default). Its backup is <KeyFile>.BAK.
.PARAMETER BoxHost
  ssh host alias for the box. Default 'jarvis'.
.PARAMETER Port
  The receiver's HTTP/SSE port, used only by gate P5. Default 8800.
.PARAMETER Yes
  Skip the typed confirmation. Exists for the operator who is re-running a
  procedure they have already read. It skips NO verification gate -- but be
  precise about what it does remove: besides the typed word, it also disables the
  redirected-stdin refusal (exit 4), because that refusal exists only to stop an
  unanswerable prompt. So `-Rekey -Yes` CAN run a device write with no human
  present, e.g. from a scheduled task or a pipe. That is the whole risk of the
  switch, and it is stated here rather than described as harmless.

.NOTES
  Exit codes:
    0  ok
    2  no mode given (usage printed)      3  more than one mode given
    4  stdin redirected (see -Check)      5  a prerequisite is missing
    6  a header constant could not be derived
   10  P1 box unreachable over ssh       11  P2 box slot invalid
   12  P3 PC key file invalid            13  P4 halves already disagree
   14  P5 a receiver is running
   20  B1 PC backup unverified           21  B2 box backup unverified
   30  G1 generation failed              31  G2 new key invalid
   32  G3 new slot invalid               33  G4 slot key != key file
   34  G5 new key == old key
   40  T1 transfer failed                41  T2 md5 differs across the wire
   50  write not confirmed               51  dd write failed
   60  W1 readback fingerprint mismatch  61  W2 readback md5 mismatch
   62  W3 NEIGHBOUR DAMAGED -- data loss, act immediately
   70  S1/S2 live key swap failed
   80  -Rollback: backups missing or unverifiable

  Transcript: %USERPROFILE%\.jarvis\admin\transcript.log -- OUTSIDE the repo,
  append-only, and it NEVER self-rotates, because a tool that edits its own audit
  log is one whose audit log cannot be trusted. It records
  timestamp | mode | RESOLVED COMMAND | exit code. Never output. Never key bytes.

  The failure signature of a key mismatch is NO REPLY AT ALL, not an error: the
  box drops the frame at HMAC and never answers, which looks exactly like a dead
  box. Do not debug it as one.

  THE ONE WINDOW, STATED RATHER THAN GLOSSED. Between the dd write and S1 the
  box holds the NEW key while this PC still holds the OLD one -- the channel is
  down for that interval by construction, because the two halves cannot be
  swapped atomically across two machines. It is covered on both sides: both
  backups still exist (nothing is cleaned up until S2 passes) and the new pair is
  in the scratch directory, so the operator can go forward (re-run) or back
  (-Rollback) from anywhere inside it. If the script is interrupted in that
  window it says nothing on the way out, so: if a run dies after "write
  completed" and before "S2", the box is on the new key and this PC is not.
  -Rollback is the way out, and the scratch directory still holds a copy of the
  live key until it is removed by hand.
#>
[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$Rekey,
    [switch]$Rollback,
    [switch]$KeepBackups,
    [switch]$Yes,
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.jarvis\control_key.bin'),
    [string]$BoxHost = 'jarvis',
    [int]$Port = 8800
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Constants. BoxDevice is deliberately NOT a parameter: this script writes to
# it, and a knob here would invite exactly the typo gate W3 exists to catch.
# ---------------------------------------------------------------------------
$BoxDevice   = '/dev/nvme0n1'
$ConfirmWord = 'REKEY-WRITE-NOW'
$RollbackWord = 'ROLLBACK-WRITE-NOW'

$repoRoot  = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$genKey    = Join-Path $PSScriptRoot 'gen_control_key.py'
$adminHome = Join-Path $env:USERPROFILE '.jarvis\admin'
$transcript = Join-Path $adminHome 'transcript.log'

# Box-side artifact names. Referred to remotely as "$HOME/<name>" inside
# SINGLE-quoted PowerShell strings so the expansion happens on the BOX, and as a
# bare relative name to scp (whose remote paths are already home-relative). A
# tilde inside a dd operand -- of=~/x -- is NOT reliably expanded across shells,
# so it is avoided entirely.
$BoxSlotNew = 'jkey_slot.new'
$BoxSlotBak = 'jkey_slot.BAK'

$PcKeyBak = "$KeyFile.BAK"

$script:worstExit = 0
$script:mode = 'none'

function Say([string]$Message)  { Write-Host $Message }
function Info([string]$Message) { Write-Host ("  info: {0}" -f $Message) }
function Pass([string]$Message) { Write-Host ("  PASS: {0}" -f $Message) -ForegroundColor Green }
function Warn([string]$Message) { Write-Host ("  warn: {0}" -f $Message) -ForegroundColor Yellow }

# Artifacts this run has created that a reader needs to know about on an abort.
# Registered as they come into existence, so an abort can name them even though
# it cannot clean them up (cleanup must not run before the final gate -- doing so
# would destroy the rollback path).
$script:artifacts = @()
$script:keyIsLive = $false      # set true the moment the dd write succeeds
function Add-Artifact {
    # -NewKey marks an artifact holding the NEWLY GENERATED key. Whether that key
    # is LIVE depends on whether the write has happened yet, which is why it is
    # resolved at print time rather than at registration.
    param([string]$Path, [string]$What, [switch]$NewKey)
    $script:artifacts += [pscustomobject]@{ Path = $Path; What = $What; NewKey = [bool]$NewKey }
}

function Show-Artifacts {
    # Printed on EVERY abort once anything exists. An abort between the write and
    # S2 leaves up to three plaintext copies of the NOW-LIVE key lying around, and
    # a script that exits silently there leaves the operator with nothing to act
    # on -- documenting the window in a comment is not the same as telling the
    # person reading the terminal.
    if (-not $script:artifacts.Count) { return }
    Write-Host ''
    Write-Host '  ARTIFACTS THIS RUN LEFT BEHIND (nothing was cleaned up -- cleanup runs only after the final gate,' -ForegroundColor Yellow
    Write-Host '  so an aborted run never destroys its own rollback path):' -ForegroundColor Yellow
    foreach ($a in $script:artifacts) {
        $tag = if ($a.NewKey -and $script:keyIsLive) { '   *** HOLDS THE LIVE KEY -- DELETE IT ***' }
               elseif ($a.NewKey) { '   (new key, NOT yet live on the box)' }
               else { '   (previous key -- your rollback path; keep until you are satisfied)' }
        Write-Host ("    {0}`n      {1}{2}" -f $a.Path, $a.What, $tag) -ForegroundColor Yellow
    }
    if ($script:keyIsLive -and ($script:artifacts | Where-Object { $_.NewKey })) {
        Write-Host '  Delete the LIVE-KEY copies once you have finished, or rolled back:' -ForegroundColor Yellow
        Write-Host ('    ssh {0} ''{1}''' -f $BoxHost, (Get-CmdBoxRemove -Name $BoxSlotNew)) -ForegroundColor Yellow
        Write-Host '    Remove-Item -Recurse -Force <the scratch dir named above>' -ForegroundColor Yellow
    }
}

function Fail([int]$Code, [string]$Message) {
    Write-Host ("  FAIL: {0}" -f $Message) -ForegroundColor Red
    if (-not $Check) {
        Write-Transcript -Command ('ABORT: {0}' -f $Message) -ExitCode $Code
        Show-Artifacts
        exit $Code
    }
    # -Check accumulates. Keep the WORST (highest) code, not merely the first --
    # otherwise a later, more serious gate is masked by an earlier trivial one.
    if ($Code -gt $script:worstExit) { $script:worstExit = $Code }
}

function Write-Transcript {
    param([string]$Command, $ExitCode)
    try {
        if (-not (Test-Path -LiteralPath $adminHome)) {
            New-Item -ItemType Directory -Path $adminHome -Force | Out-Null
        }
        $line = '{0} | {1} | {2} | exit={3}' -f `
                (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK'), $script:mode, $Command, $ExitCode
        Add-Content -LiteralPath $transcript -Value $line -Encoding UTF8
    } catch {
        Write-Host ("  warn: could not write the transcript ({0})" -f $_.Exception.Message) -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# Mode selection. No default, and exactly one.
# ---------------------------------------------------------------------------
$modeCount = @($Check, $Rekey, $Rollback | Where-Object { $_ }).Count
if ($modeCount -eq 0) {
    Say ''
    Say 'jarvis_admin.ps1 -- the DESTRUCTIVE half of the operator tooling (control-IN re-key).'
    Say ''
    Say '  jarvis_admin.bat -Check       validate everything, print what each step would run,'
    Say '                                change nothing. Safe at any time. Start here.'
    Say '  jarvis_admin.bat -Rekey       generate + provision a new key pair (typed confirmation)'
    Say '  jarvis_admin.bat -Rollback    restore the previous pair from the .BAK artifacts'
    Say ''
    Say '  -KeepBackups   keep the old pair after a successful re-key (default: delete)'
    Say '  -KeyFile PATH  the live 32-byte key (default %USERPROFILE%\.jarvis\control_key.bin)'
    Say ''
    Say 'No mode was given. A destructive tool does nothing on a bare invocation.'
    Say ''
    Write-Transcript -Command 'invoked with no mode (usage printed)' -ExitCode 2
    exit 2
}
if ($modeCount -gt 1) {
    Write-Host '  FAIL: -Check, -Rekey and -Rollback are mutually exclusive -- pick one' -ForegroundColor Red
    Write-Transcript -Command 'more than one mode given' -ExitCode 3
    exit 3
}
$script:mode = if ($Check) { 'check' } elseif ($Rekey) { 'rekey' } else { 'rollback' }

# The confirmed modes prompt. With stdin redirected Read-Host returns empty
# immediately rather than blocking (jarvis_menu.ps1 shipped an infinite loop from
# exactly this), and an empty answer must never be able to satisfy a
# confirmation. Refuse up front and name the mode that IS non-interactive.
if (-not $Check -and -not $Yes -and [Console]::IsInputRedirected) {
    Write-Host '  FAIL: stdin is redirected, so the typed confirmation cannot be answered.' -ForegroundColor Red
    Write-Host '        Run this in a console (double-click jarvis_admin.bat), or use -Check,' -ForegroundColor Red
    Write-Host '        which validates everything and runs nothing.' -ForegroundColor Red
    Write-Transcript -Command 'refused: stdin redirected in a confirmed mode' -ExitCode 4
    exit 4
}

# ---------------------------------------------------------------------------
# Header constants -- PARSED, never retyped.
# ---------------------------------------------------------------------------
function Get-HeaderConst {
    # Handles decimal and 0x-hex with any u/U/l/L suffix combination, which the
    # menu's integer-only parser does not: CTRL_KEY_BASE_LBA is 21130000ULL and
    # CTRL_KEY_MAGIC is 0x4A4B4559u.
    param([string]$Path, [string]$Name)
    if (-not (Test-Path -LiteralPath $Path)) { throw ("header not found: {0}" -f $Path) }
    $pattern = '^\s*#define\s+{0}\s+(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*\s*(/\*|$)' -f [regex]::Escape($Name)
    $m = Select-String -LiteralPath $Path -Pattern $pattern | Select-Object -First 1
    if (-not $m) { throw ("could not derive {0} from {1}" -f $Name, $Path) }
    $raw = $m.Matches[0].Groups[1].Value
    if ($raw -match '^0[xX]') { return [uint64]([Convert]::ToUInt64($raw.Substring(2), 16)) }
    return [uint64]$raw
}

function Get-HeaderLbaOffset {
    # The neighbour LBAs are EXPRESSIONS in their headers -- (CTRL_KEY_BASE_LBA +
    # 1ULL) -- so the offset is parsed out of the expression rather than assumed.
    # If the expression shape ever changes this fails loudly instead of guessing.
    param([string]$Path, [string]$Name, [string]$BaseMacro)
    if (-not (Test-Path -LiteralPath $Path)) { throw ("header not found: {0}" -f $Path) }
    $pattern = '^\s*#define\s+{0}\s+\(\s*{1}\s*\+\s*([0-9]+)[uUlL]*\s*\)' -f `
               [regex]::Escape($Name), [regex]::Escape($BaseMacro)
    $m = Select-String -LiteralPath $Path -Pattern $pattern | Select-Object -First 1
    if (-not $m) { throw ("could not derive {0} from {1}" -f $Name, $Path) }
    [uint64]$m.Matches[0].Groups[1].Value
}

function Get-LeMagicHex {
    # The magic is a uint32 stored little-endian, so on disk 0x4A4B4559 reads as
    # bytes 59 45 4b 4a. Comparing HEX rather than ASCII sidesteps that reversal
    # entirely -- "JKEY" on disk is the byte string "YEKJ", which is the sort of
    # detail that turns into a wrong assertion at 1am.
    param([uint64]$Magic)
    $b = [BitConverter]::GetBytes([uint32]$Magic)   # little-endian on x86/x64
    (($b | ForEach-Object { $_.ToString('x2') }) -join '')
}

$hdrKey     = Join-Path $repoRoot 'phase3\src\net\control_key.h'
$hdrFloor   = Join-Path $repoRoot 'phase3\src\net\control_floor.h'
$hdrConsole = Join-Path $repoRoot 'phase3\src\net\control_console.h'

try {
    $KEY_LBA     = Get-HeaderConst -Path $hdrKey -Name 'CTRL_KEY_BASE_LBA'
    $KEY_MAGIC   = Get-HeaderConst -Path $hdrKey -Name 'CTRL_KEY_MAGIC'
    $KEY_VERSION = Get-HeaderConst -Path $hdrKey -Name 'CTRL_KEY_VERSION'
    $KEY_LEN     = Get-HeaderConst -Path $hdrKey -Name 'CTRL_KEY_LEN'
    $FLOOR_MAGIC = Get-HeaderConst -Path $hdrFloor -Name 'CTRL_FLOOR_MAGIC'
    $CONS_MAGIC  = Get-HeaderConst -Path $hdrConsole -Name 'CTRL_CONSOLE_MAGIC'
    $offFloorA   = Get-HeaderLbaOffset -Path $hdrFloor -Name 'CTRL_FLOOR_LBA_A' -BaseMacro 'CTRL_KEY_BASE_LBA'
    $offFloorB   = Get-HeaderLbaOffset -Path $hdrFloor -Name 'CTRL_FLOOR_LBA_B' -BaseMacro 'CTRL_KEY_BASE_LBA'
    $offConsole  = Get-HeaderLbaOffset -Path $hdrConsole -Name 'CTRL_CONSOLE_LBA' -BaseMacro 'CTRL_KEY_BASE_LBA'
} catch {
    Write-Host ("  FAIL: {0}" -f $_.Exception.Message) -ForegroundColor Red
    Write-Host '        Every LBA and magic in this script is parsed from the firmware headers.' -ForegroundColor Red
    exit 6
}

$SLOT_BYTES = 512
$KEY_OFFSET = 8      # ctrl_key_slot_t: magic(4) version(2) reserved0(2) then key

$neighbours = @(
    [pscustomobject]@{ Lba = $KEY_LBA + $offFloorA; Name = 'JFLR (replay floor A)'; Hex = (Get-LeMagicHex $FLOOR_MAGIC) }
    [pscustomobject]@{ Lba = $KEY_LBA + $offFloorB; Name = 'JFLR (replay floor B)'; Hex = (Get-LeMagicHex $FLOOR_MAGIC) }
    [pscustomobject]@{ Lba = $KEY_LBA + $offConsole; Name = 'JCON (console address)'; Hex = (Get-LeMagicHex $CONS_MAGIC) }
)
$KEY_MAGIC_HEX = Get-LeMagicHex $KEY_MAGIC

# ---------------------------------------------------------------------------
# Resolved commands. ONE source for both -Check ("would run") and the runners
# ("ran"), so printed and executed cannot drift -- the menu's R1 enforcement.
#
# Remote strings use SINGLE-quoted PowerShell format strings wherever "$HOME"
# appears, so it expands on the box and not here.
# ---------------------------------------------------------------------------
# NO EMBEDDED DOUBLE QUOTES IN ANY REMOTE COMMAND, and that is not style.
# Windows PowerShell 5.1's native-argument escaping does not reliably survive a
# double quote inside an argument, so `tr -d " \n"` and `cut -d" " -f1` reached
# the box with their quotes eaten and SILENTLY became no-ops -- the first -Check
# run printed a magic of '59 45 4b 4a' because the tr never deleted anything.
# The fixes: strip whitespace LOCALLY instead of remotely, take fixed-width hash
# prefixes with `cut -c` instead of a delimiter, and leave $HOME/<name>
# unquoted (the path has no spaces, and $VAR expansion -- unlike tilde -- happens
# anywhere in an unquoted word, including after `if=`).

function Get-CmdSectorMagic { param([uint64]$Lba)
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count=1 status=none | od -An -tx1 -N4' -f $BoxDevice, $Lba }

function Get-CmdSectorFp { param([uint64]$Lba)
    # tail -c +N is 1-based, so key offset 8 is "+9". Only the 8-hex-character
    # fingerprint crosses ssh -- never the key.
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count=1 status=none | tail -c +{2} | head -c {3} | sha256sum | cut -c1-8' -f `
        $BoxDevice, $Lba, ($KEY_OFFSET + 1), $KEY_LEN }

function Get-CmdSectorMd5 { param([uint64]$Lba)
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count=1 status=none | md5sum | cut -c1-32' -f $BoxDevice, $Lba }

function Get-CmdSectorZero { param([uint64]$Lba)
    # "is the key all zero" without moving key bytes: count the non-zero ones.
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count=1 status=none | tail -c +{2} | head -c {3} | tr -d \\000 | wc -c' -f `
        $BoxDevice, $Lba, ($KEY_OFFSET + 1), $KEY_LEN }

function Get-CmdBoxBackup {
    # The redirect is performed by the LOGIN shell, not by sudo, so the backup
    # lands owned by the ssh user rather than root -- which keeps cleanup a plain
    # rm. (A dd of=~/... operand would also be a tilde-expansion gamble: tilde
    # expansion after `=` in a command argument is not portable, $HOME is.)
    param([string]$Name)
    'sudo -n dd if={0} bs=512 skip={1} count=1 status=none > $HOME/{2}' -f $BoxDevice, $KEY_LBA, $Name }

function Get-CmdBoxFileSize { param([string]$Name) 'stat -c%s $HOME/{0}' -f $Name }

function Get-CmdBoxFileMagic { param([string]$Name)
    'set -o pipefail; head -c 4 $HOME/{0} | od -An -tx1' -f $Name }

function Get-CmdBoxFileFp { param([string]$Name)
    'set -o pipefail; tail -c +{0} $HOME/{1} | head -c {2} | sha256sum | cut -c1-8' -f ($KEY_OFFSET + 1), $Name, $KEY_LEN }

function Get-CmdBoxFileMd5 { param([string]$Name) 'set -o pipefail; md5sum $HOME/{0} | cut -c1-32' -f $Name }

function Get-CmdBoxWrite { param([string]$Name)
    'sudo -n dd if=$HOME/{0} of={1} bs=512 seek={2} count=1 conv=fsync' -f $Name, $BoxDevice, $KEY_LBA }

function Get-CmdBoxRemove { param([string]$Name)
    # rm first (the file is normally ours); sudo only if that fails. Never fails
    # the run -- cleanup reports, it does not abort a completed re-key.
    'rm -f $HOME/{0} 2>/dev/null || sudo -n rm -f $HOME/{0}' -f $Name }

# sha256("")[:8]. A fingerprint gate whose `dd` produced NO BYTES hashes the
# empty stream and returns exactly this, so seeing it means "the read returned
# nothing", not "the key is wrong". Measured, not assumed: a deliberately failed
# remote read returns 'e3b0c442'. `set -o pipefail` now makes such a read exit
# non-zero as well, but the hint stays because it names the cause in one line.
$FP_OF_EMPTY = 'e3b0c442'

function Get-EmptyReadHint {
    param([string]$Fp)
    if ($Fp -eq $FP_OF_EMPTY) {
        return ' -- NOTE: that is sha256 of an EMPTY read, so the sector read produced no bytes at all (permissions, wrong device, sudo). It is not a wrong key.'
    }
    return ''
}

function Get-HexOut {
    # Remote hex arrives space-separated from od; normalise HERE rather than
    # asking the remote shell to do it inside quotes (see the note above).
    param([string]$Raw)
    ($Raw -replace '\s', '').ToLower() }

function Get-CmdGen { param([string]$ScratchDir)
    # --force is never passed: generation goes to a fresh scratch dir, which
    # keeps gen_control_key.py's own fail-closed clobber guard armed.
    'py -3 "{0}" --key-out "{1}" --slot-out "{2}"' -f `
        $genKey, (Join-Path $ScratchDir 'control_key.bin'), (Join-Path $ScratchDir 'jkey_slot.bin') }

function Get-CmdScp { param([string]$ScratchDir)
    # Run from the scratch dir with a BARE filename. scp's syntax is host:path,
    # so a Windows source path makes it read "C" as a hostname -- this bit during
    # the manual run. A bare name has no colon to misparse.
    'cd "{0}"; scp {1} {2}:{3}' -f $ScratchDir, 'jkey_slot.bin', $BoxHost, $BoxSlotNew }

# ---------------------------------------------------------------------------
# Execution helpers
# ---------------------------------------------------------------------------
function Invoke-Box {
    # Returns [pscustomobject]{ Out; Code }. Never throws on a non-zero remote
    # exit -- callers decide which gate that is.
    # NOTE: deliberately NO 2>&1. In Windows PowerShell 5.1 redirecting a native
    # command's stderr wraps each line in a NativeCommandError ErrorRecord and
    # sets $? false even on exit 0 -- with $ErrorActionPreference='Stop' that can
    # abort the script mid-procedure. Remote stderr goes straight to the console
    # where the operator can see it; only stdout is parsed.
    param([string]$RemoteCommand)
    $out = & ssh @('-o','BatchMode=yes','-o','ConnectTimeout=8', $BoxHost, $RemoteCommand)
    $code = $LASTEXITCODE
    Write-Transcript -Command ('ssh {0} "{1}"' -f $BoxHost, $RemoteCommand) -ExitCode $code
    [pscustomobject]@{ Out = (@($out) -join "`n").Trim(); Code = $code }
}

function Get-FpFromBytes {
    # sha256(key)[:8]. Takes bytes, returns 8 hex characters. The bytes never
    # leave this function.
    param([byte[]]$Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $h = $sha.ComputeHash($Bytes) } finally { $sha.Dispose() }
    (($h[0..3] | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Get-FileMd5 { param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm MD5).Hash.ToLower() }

function Test-ReceiverRunning {
    # The receiver serves the console over loopback HTTP; if that port answers,
    # something is holding a key it loaded at startup.
    try {
        $c = New-Object Net.Sockets.TcpClient
        $iar = $c.BeginConnect('127.0.0.1', $Port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(700)
        if ($ok) { try { $c.EndConnect($iar) } catch { $ok = $false } }
        $c.Close()
        return $ok
    } catch { return $false }
}

# ---------------------------------------------------------------------------
# Preflight (P1..P5). REALLY RUNS in every mode, including -Check: these are all
# reads, and a -Check that validated nothing it could actually touch is how the
# menu's -Lines bug shipped.
# ---------------------------------------------------------------------------
function Invoke-Preflight {
    Say ''
    Say '== preflight =='
    $toolsOk = $true

    foreach ($t in @(
        @{ n='ssh';  m='ssh not found on PATH (Windows: Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0)' },
        @{ n='scp';  m='scp not found on PATH -- it ships with the same OpenSSH client as ssh' },
        @{ n='py';   m="the 'py' launcher was not found -- gen_control_key.py runs as py -3" })) {
        if (Get-Command $t.n -ErrorAction SilentlyContinue) { Pass ("{0} present" -f $t.n) }
        else { Fail 5 $t.m; $toolsOk = $false }
    }
    if (Test-Path -LiteralPath $genKey) { Pass ("generator present ({0})" -f $genKey) }
    else { Fail 5 ("gen_control_key.py not found at '{0}'" -f $genKey) }

    Pass ('LBA {0} / magic 0x{1:X} / version {2} / key {3} B -- all parsed from control_key.h' -f `
          $KEY_LBA, $KEY_MAGIC, $KEY_VERSION, $KEY_LEN)
    foreach ($n in $neighbours) { Info ('neighbour LBA {0} must stay {1}' -f $n.Lba, $n.Name) }

    # In -Check, Fail() accumulates instead of exiting -- so without this return
    # a missing `ssh` fell straight through to `& ssh` below and died with an
    # unhandled CommandNotFoundException, printing no report at all. The tool
    # failed to diagnose the one environment defect it most needs to.
    if (-not $toolsOk) {
        Info 'skipping every box check: a required tool is missing (see the FAIL lines above)'
        return $null
    }

    # -- P1 -----------------------------------------------------------------
    $probe = Invoke-Box -RemoteCommand 'true'
    if ($probe.Code -ne 0) {
        Fail 10 ("ssh to '{0}' failed. THE BOX MUST BE ON UBUNTU. If it is running JARVIS there is no sshd, and re-keying a running box is never correct; if it is off, turn it on. Do not guess -- check the machine." -f $BoxHost)
        return $null
    }
    Pass ("box reachable over ssh ('{0}' answered)" -f $BoxHost)

    # -- P2 -----------------------------------------------------------------
    $magic = Invoke-Box -RemoteCommand (Get-CmdSectorMagic -Lba $KEY_LBA)
    if ($magic.Code -ne 0) {
        Fail 11 ("could not read the key sector: {0}" -f $magic.Out); return $null
    }
    $magicHex = Get-HexOut $magic.Out
    if ($magicHex -ne $KEY_MAGIC_HEX) {
        Fail 11 ("key sector at LBA {0} has magic '{1}', expected '{2}' (JKEY, little-endian on disk). The slot is absent or damaged -- this is a PROVISIONING job, not a re-key." -f $KEY_LBA, $magicHex, $KEY_MAGIC_HEX)
        return $null
    }
    Pass ('box slot magic OK at LBA {0}' -f $KEY_LBA)

    $nz = Invoke-Box -RemoteCommand (Get-CmdSectorZero -Lba $KEY_LBA)
    # Split deliberately: before `set -o pipefail` these two collapsed into one
    # message, and a FAILED read reported "the key is ALL ZERO" -- a wrong
    # diagnosis in an incident-response tool, because a broken pipeline yields
    # an empty stream and `wc -c` then honestly counts 0.
    if ($nz.Code -ne 0) {
        Fail 11 ("the remote read of the key sector FAILED (exit {0}) -- this is a read failure, NOT a verdict about the key's contents" -f $nz.Code)
        return $null
    }
    if ([int]$nz.Out -eq 0) {
        Fail 11 'the key inside the box slot is ALL ZERO -- the box treats that as no key (fail-closed) and the channel is already down'
        return $null
    }
    $fpBoxR = Invoke-Box -RemoteCommand (Get-CmdSectorFp -Lba $KEY_LBA)
    if ($fpBoxR.Code -ne 0 -or $fpBoxR.Out.Length -ne 8) {
        Fail 11 ("could not fingerprint the box key (exit {0}, output '{1}'){2}" -f $fpBoxR.Code, $fpBoxR.Out, (Get-EmptyReadHint $fpBoxR.Out)); return $null
    }
    $fpBox = $fpBoxR.Out
    Pass ('FP_BOX = {0}' -f $fpBox)

    # -- P3 -----------------------------------------------------------------
    if (-not (Test-Path -LiteralPath $KeyFile)) {
        Fail 12 ("PC key file '{0}' not found -- nothing to compare against, and nothing to back up" -f $KeyFile)
        return $null
    }
    $pcLen = (Get-Item -LiteralPath $KeyFile).Length
    if ($pcLen -ne [int]$KEY_LEN) {
        Fail 12 ("PC key file '{0}' is {1} bytes, expected exactly {2} (raw key, not hex/base64)" -f $KeyFile, $pcLen, $KEY_LEN)
        return $null
    }
    $fpPc = Get-FpFromBytes ([IO.File]::ReadAllBytes($KeyFile))
    Pass ('PC key file exactly {0} bytes -- FP_PC = {1}' -f $KEY_LEN, $fpPc)

    # -- P4 -- the check the runbook does not have --------------------------
    if ($fpBox -ne $fpPc) {
        Fail 13 ("THE TWO HALVES ALREADY DISAGREE: box {0} != PC {1}. The channel is broken RIGHT NOW, and re-keying is the wrong action -- it would overwrite the only copy of the box's current key. Restore a matching pair first: jarvis_admin.bat -Rollback" -f $fpBox, $fpPc)
        return $null
    }
    Pass ('P4 the halves MATCH ({0}) -- the channel is healthy, so a re-key is a safe operation' -f $fpBox)

    # -- P5 -----------------------------------------------------------------
    if (Test-ReceiverRunning) {
        Fail 14 ("something is answering on 127.0.0.1:{0} -- a receiver loads the key ONCE at startup, so one left running would keep signing with the OLD key after the re-key and look exactly like a dead box. Stop it, then re-run." -f $Port)
    } else {
        Pass ('no receiver answering on 127.0.0.1:{0}' -f $Port)
    }

    [pscustomobject]@{ FpBox = $fpBox; FpPc = $fpPc }
}

# ---------------------------------------------------------------------------
# Neighbour check (W3) -- also useful on its own, so -Check runs it too.
# ---------------------------------------------------------------------------
function Test-Neighbours {
    param([switch]$Fatal)
    $allOk = $true
    foreach ($n in $neighbours) {
        $r = Invoke-Box -RemoteCommand (Get-CmdSectorMagic -Lba $n.Lba)
        $got = Get-HexOut $r.Out
        if ($r.Code -ne 0) {
            # Split from the damage branch deliberately: funnelling both into one
            # message means a transient ssh/sudo failure prints the most alarming
            # line in the tool about a box that is fine.
            $allOk = $false
            Fail 62 ("could not READ neighbour LBA {0} ({1}) -- exit {2}. This is an ssh/sudo READ failure; the sector is NOT proven damaged. Re-run before acting." -f $n.Lba, $n.Name, $r.Code)
        } elseif ($got -ne $n.Hex) {
            $allOk = $false
            if ($Fatal) {
                Fail 62 ("NEIGHBOUR DAMAGED: LBA {0} should hold {1} (magic '{2}') and reads '{3}'. THIS IS A DATA-LOSS EVENT, not a re-key failure. A wrong seek= digit lands on the replay floor, the console address, the JACT audit store or the control-IN conversation store. Stop and assess before booting JARVIS." -f $n.Lba, $n.Name, $n.Hex, $got)
            } else {
                Fail 62 ("neighbour LBA {0} ({1}) reads '{2}', expected '{3}'" -f $n.Lba, $n.Name, $got, $n.Hex)
            }
        } else {
            Pass ('neighbour LBA {0} intact -- {1}' -f $n.Lba, $n.Name)
        }
    }
    return $allOk
}

# ---------------------------------------------------------------------------
# -Check's DRY RUN. Both bugs found in this script were found by RUNNING a
# remote command, not by reading one -- so a -Check that only PRINTS the nine
# commands it never executes leaves that entire class live until the destructive
# run, at a point where both backups have already been overwritten.
#
# This exercises every remote string except the two that cannot be run without
# writing the device (Get-CmdBoxWrite) -- against a PROBE FILE, never the real
# backup name, so a -Check can never clobber a rollback path.
# ---------------------------------------------------------------------------
function Invoke-CheckDryRun {
    $probeName = 'jarvis_admin_probe.tmp'
    $ok = $true
    Say ''
    Say '== dry run: really executing every remote command shape (probe file, never the real backup) =='

    $r = Invoke-Box -RemoteCommand (Get-CmdBoxBackup -Name $probeName)      # exercises the > redirect
    if ($r.Code -ne 0) { Fail 5 ("the backup command shape FAILED (exit {0}) -- the '>' redirect or sudo is broken" -f $r.Code); $ok = $false }
    else { Pass 'backup shape (sudo dd > $HOME/file) works' }

    if ($ok) {
        $sz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $probeName)   # exercises %s
        if ($sz.Code -ne 0 -or [int]$sz.Out -ne $SLOT_BYTES) { Fail 5 ("size shape returned '{0}', expected {1}" -f $sz.Out, $SLOT_BYTES); $ok = $false }
        else { Pass ('size shape (stat -c%s) works -- {0} bytes' -f $sz.Out) }

        $mg = Invoke-Box -RemoteCommand (Get-CmdBoxFileMagic -Name $probeName)
        if ($mg.Code -ne 0 -or (Get-HexOut $mg.Out) -ne $KEY_MAGIC_HEX) { Fail 5 ("file-magic shape returned '{0}'" -f (Get-HexOut $mg.Out)); $ok = $false }
        else { Pass 'file-magic shape (head -c 4 | od) works' }

        $fp = Invoke-Box -RemoteCommand (Get-CmdBoxFileFp -Name $probeName)
        if ($fp.Code -ne 0 -or $fp.Out.Length -ne 8) { Fail 5 ("file-fingerprint shape returned '{0}'{1}" -f $fp.Out, (Get-EmptyReadHint $fp.Out)); $ok = $false }
        else { Pass ('file-fingerprint shape (tail|head|sha256sum|cut) works -- {0}' -f $fp.Out) }

        $m5 = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $probeName)
        if ($m5.Code -ne 0 -or $m5.Out.Length -ne 32) { Fail 5 ("file-md5 shape returned '{0}'" -f $m5.Out); $ok = $false }
        else { Pass ('file-md5 shape (md5sum|cut) works -- {0}' -f $m5.Out) }

        $sm = Invoke-Box -RemoteCommand (Get-CmdSectorMd5 -Lba $KEY_LBA)
        if ($sm.Code -ne 0 -or $sm.Out -ne $m5.Out) { Fail 5 ("sector-md5 shape returned '{0}', expected the probe's {1}" -f $sm.Out, $m5.Out); $ok = $false }
        else { Pass ('sector-md5 shape works, and AGREES with the file copy ({0})' -f $sm.Out) }
    }

    # exercises `rm -f ... 2>/dev/null || sudo -n rm -f ...`, then PROVES removal
    $rm = Invoke-Box -RemoteCommand (Get-CmdBoxRemove -Name $probeName)
    $gone = Invoke-Box -RemoteCommand ('test ! -e $HOME/{0}' -f $probeName)
    if ($gone.Code -ne 0) { Fail 5 ("the probe file was NOT removed -- delete ~/{0} by hand" -f $probeName); $ok = $false }
    else { Pass 'remove shape works, and the probe file is PROVEN gone (test ! -e)' }

    # scp shape: a tiny temp file under the probe name, then removed
    $sd = Join-Path $env:TEMP ('jarvis-admin-scp-{0}' -f (Get-Random))
    New-Item -ItemType Directory -Path $sd -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $sd 'jkey_slot.bin') -Value 'probe' -Encoding ASCII
    Push-Location $sd
    try { & scp 'jkey_slot.bin' ("{0}:{1}" -f $BoxHost, $probeName) | Out-Null; $srr = $LASTEXITCODE } finally { Pop-Location }
    Remove-Item -LiteralPath $sd -Recurse -Force -ErrorAction SilentlyContinue
    if ($srr -ne 0) { Fail 5 ("the scp shape FAILED (exit {0}) -- host:path parsing or auth" -f $srr); $ok = $false }
    else { Pass 'scp shape (bare filename from the scratch dir) works' }
    [void](Invoke-Box -RemoteCommand (Get-CmdBoxRemove -Name $probeName))

    # generator + G2/G3/G4, into a temp dir, never the live paths, never --force
    $gd = Join-Path $env:TEMP ('jarvis-admin-gen-{0}' -f (Get-Random))
    New-Item -ItemType Directory -Path $gd -Force | Out-Null
    $gk = Join-Path $gd 'control_key.bin'; $gs = Join-Path $gd 'jkey_slot.bin'
    & py -3 $genKey --key-out $gk --slot-out $gs | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $gs)) {
        Fail 30 'the generator FAILED in the dry run -- G1 would abort a real -Rekey'; $ok = $false
    } else {
        $gkb = [IO.File]::ReadAllBytes($gk); $gsb = [IO.File]::ReadAllBytes($gs)
        $gsk = New-Object byte[] ([int]$KEY_LEN); [Array]::Copy($gsb, $KEY_OFFSET, $gsk, 0, [int]$KEY_LEN)
        if ($gkb.Length -ne [int]$KEY_LEN -or $gsb.Length -ne $SLOT_BYTES) { Fail 31 'dry-run generator produced wrong-sized artifacts'; $ok = $false }
        elseif ([BitConverter]::ToUInt32($gsb,0) -ne [uint32]$KEY_MAGIC) { Fail 32 'dry-run slot has the wrong magic'; $ok = $false }
        elseif ((Get-FpFromBytes $gsk) -ne (Get-FpFromBytes $gkb)) { Fail 33 'dry-run slot key != key file (G4 would abort)'; $ok = $false }
        else { Pass 'generator + G2/G3/G4 verified on a throwaway pair (deleted immediately)' }
    }
    Remove-Item -LiteralPath $gd -Recurse -Force -ErrorAction SilentlyContinue

    if ($ok) { Pass 'every remote command shape except the device WRITE has been executed for real' }
    Info 'NOT exercised, and cannot be without writing the device: the dd write itself (of=/seek=)'
    return $ok
}

# ===========================================================================
# -Check
# ===========================================================================
if ($Check) {
    Say '== jarvis_admin.ps1 -Check (validates everything, runs nothing destructive) =='
    $pf = Invoke-Preflight
    Say ''
    Say '== neighbour sectors (the W3 gate, read-only) =='
    if ($pf) { [void](Test-Neighbours) } else { Info 'skipped: preflight did not reach a usable box' }

    try {
        if (-not (Test-Path -LiteralPath $adminHome)) { New-Item -ItemType Directory -Path $adminHome -Force | Out-Null }
        Pass ("transcript directory writable ({0})" -f $adminHome)
    } catch { Fail 5 ("cannot create '{0}'" -f $adminHome) }

    $scratchShown = Join-Path $env:TEMP 'jarvis-rekey-<timestamp>'
    if ($pf) { [void](Invoke-CheckDryRun) } else { Info 'skipping the dry run: preflight did not reach a usable box' }

    Say ''
    Say '-- what -Rekey WOULD run, in order (the shapes above were executed; the WRITE was not) --'
    Say ('  G1 generate  : {0}' -f (Get-CmdGen -ScratchDir $scratchShown))
    Say ('  B1 PC backup : copy "{0}" -> "{1}"  (then verify {2} B + fingerprint)' -f $KeyFile, $PcKeyBak, $KEY_LEN)
    Say ('  B2 box backup: ssh {0} "{1}"' -f $BoxHost, (Get-CmdBoxBackup -Name $BoxSlotBak))
    Say ('     verify    : ssh {0} "{1}" / "{2}"' -f $BoxHost, (Get-CmdBoxFileSize -Name $BoxSlotBak), (Get-CmdBoxFileFp -Name $BoxSlotBak))
    Say ('  T1 transfer  : {0}' -f (Get-CmdScp -ScratchDir $scratchShown))
    Say ('  T2 md5 both  : ssh {0} "{1}"   vs the local slot' -f $BoxHost, (Get-CmdBoxFileMd5 -Name $BoxSlotNew))
    Say ('     CONFIRM   : the operator must type {0} exactly (case-sensitive)' -f $ConfirmWord)
    Say ('  -- WRITE     : ssh {0} "{1}"' -f $BoxHost, (Get-CmdBoxWrite -Name $BoxSlotNew))
    Say ('  W1 readback  : ssh {0} "{1}"' -f $BoxHost, (Get-CmdSectorFp -Lba $KEY_LBA))
    Say ('  W2 md5       : ssh {0} "{1}"' -f $BoxHost, (Get-CmdSectorMd5 -Lba $KEY_LBA))
    foreach ($n in $neighbours) {
        Say ('  W3 neighbour : ssh {0} "{1}"   (expect {2})' -f $BoxHost, (Get-CmdSectorMagic -Lba $n.Lba), $n.Hex)
    }
    Say ('  S1 swap      : copy "{0}\control_key.bin" -> "{1}"' -f $scratchShown, $KeyFile)
    Say ('  S2 verify    : fingerprint of "{0}" == FP_NEW == on-device fingerprint' -f $KeyFile)
    $cleanupNote = if ($KeepBackups) { 'the two .BAK artifacts KEPT (-KeepBackups)' } else { 'the two .BAK artifacts DELETED (default)' }
    Say ('  -- cleanup   : ssh {0} "{1}"' -f $BoxHost, (Get-CmdBoxRemove -Name $BoxSlotNew))
    Say ('                 remove the scratch dir; {0}' -f $cleanupNote)
    Say '                 (cleanup runs ONLY after S2 passes, so an aborted run keeps its rollback path)'
    Say ''
    if ($script:worstExit -eq 0) {
        Say '== -Check PASS -- a -Rekey could proceed right now =='
    } else {
        Say ("== -Check FAIL (exit {0}) -- fix the FAIL lines above before -Rekey ==" -f $script:worstExit)
    }
    Write-Transcript -Command '-Check (no destructive step run)' -ExitCode $script:worstExit
    exit $script:worstExit
}

# ===========================================================================
# -Rollback
# ===========================================================================
if ($Rollback) {
    Say '== jarvis_admin.ps1 -Rollback =='
    Say ''
    Say '  Restores the PREVIOUS key pair from the .BAK artifacts. This is for an'
    Say '  ABORTED re-key (whose backups are still in place) or one that verified'
    Say '  but behaved badly. After a successful -Rekey the backups are deleted by'
    Say '  default, so there may be nothing to roll back to -- that is intended.'
    Say ''

    if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) { Fail 5 'ssh not found on PATH' }

    if (-not (Test-Path -LiteralPath $PcKeyBak)) {
        Fail 80 ("PC backup '{0}' not found -- nothing to restore the Main-PC half from" -f $PcKeyBak)
    }
    $bakLen = (Get-Item -LiteralPath $PcKeyBak).Length
    if ($bakLen -ne [int]$KEY_LEN) {
        Fail 80 ("PC backup '{0}' is {1} bytes, expected {2} -- refusing to restore a truncated key" -f $PcKeyBak, $bakLen, $KEY_LEN)
    }
    $fpBak = Get-FpFromBytes ([IO.File]::ReadAllBytes($PcKeyBak))
    Pass ('PC backup present, {0} bytes -- fingerprint {1}' -f $KEY_LEN, $fpBak)

    $probe = Invoke-Box -RemoteCommand 'true'
    if ($probe.Code -ne 0) { Fail 10 ("ssh to '{0}' failed -- the box must be on Ubuntu" -f $BoxHost) }

    $bs = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $BoxSlotBak)
    if ($bs.Code -ne 0 -or [int]$bs.Out -ne $SLOT_BYTES) {
        Fail 80 ("box backup ~/{0} is missing or not {1} bytes (got '{2}'). NOTE the runbook says to confirm a backup is 32 bytes -- that is the KEY FILE size; the SLOT is {1}." -f $BoxSlotBak, $SLOT_BYTES, $bs.Out)
    }
    $bfp = Invoke-Box -RemoteCommand (Get-CmdBoxFileFp -Name $BoxSlotBak)
    if ($bfp.Code -ne 0 -or $bfp.Out -ne $fpBak) {
        Fail 80 ("box backup fingerprint '{0}' != PC backup fingerprint '{1}' -- the two halves of the backup do not agree, so restoring them would recreate a broken channel" -f $bfp.Out, $fpBak)
    }
    # A 512-byte file whose KEY bytes are right can still have a torn HEADER --
    # e.g. an interrupted B2 redirect. Restoring that writes a slot the box
    # rejects at boot ("no key (fail-closed) - inert"), producing the documented
    # no-reply signature while this script says "restored and verified".
    # -Rekey catches this at P2/G3; rollback checked neither magic nor md5.
    $bmg = Invoke-Box -RemoteCommand (Get-CmdBoxFileMagic -Name $BoxSlotBak)
    $bmgHex = Get-HexOut $bmg.Out
    if ($bmg.Code -ne 0 -or $bmgHex -ne $KEY_MAGIC_HEX) {
        Fail 80 ("box backup ~/{0} has magic '{1}', expected '{2}' (JKEY) -- its header is damaged even though its key bytes look right. Restoring it would leave the box fail-closed and silent." -f $BoxSlotBak, $bmgHex, $KEY_MAGIC_HEX)
    }
    Pass ('box backup magic OK ({0})' -f $KEY_MAGIC_HEX)

    $bmd5 = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $BoxSlotBak)
    if ($bmd5.Code -ne 0 -or $bmd5.Out.Length -ne 32) {
        Fail 80 ("could not md5 the box backup (exit {0}, output '{1}')" -f $bmd5.Code, $bmd5.Out)
    }
    Pass ('box backup present, {0} bytes, fingerprint {1}, md5 {2} -- both halves agree' -f $SLOT_BYTES, $bfp.Out, $bmd5.Out)

    # P5 applies to a rollback exactly as it does to a re-key: a receiver caches
    # the key at startup, so one left running keeps signing with the key you are
    # rolling AWAY from -- and it may also hold $KeyFile open against the copy.
    if (Test-ReceiverRunning) {
        Fail 14 ("something is answering on 127.0.0.1:{0} -- a receiver holds the key it loaded at startup and may hold '{1}' open. Stop it, then re-run." -f $Port, $KeyFile)
    } else {
        Pass ('no receiver answering on 127.0.0.1:{0}' -f $Port)
    }

    Say ''
    Say ('  This will WRITE ~/{0} back over LBA {1} on {2} and restore "{3}".' -f $BoxSlotBak, $KEY_LBA, $BoxDevice, $KeyFile)
    if (-not $Yes) {
        $answer = Read-Host ("  type {0} to proceed" -f $RollbackWord)
        if (-not ("$answer".Trim() -ceq $RollbackWord)) {
            Say '  not confirmed -- nothing was written.'
            Write-Transcript -Command 'rollback NOT confirmed' -ExitCode 50
            exit 50
        }
    } else {
        Warn '-Yes: typed confirmation SKIPPED, and the redirected-stdin refusal with it. No verification gate is skipped, but this write can now proceed with no human present.'
    }

    $w = Invoke-Box -RemoteCommand (Get-CmdBoxWrite -Name $BoxSlotBak)
    if ($w.Code -ne 0) { Fail 51 ("the rollback dd FAILED: {0}" -f $w.Out) }
    Pass 'box slot restored'

    # Guarded: this runs AFTER the device is already written, so an unhandled
    # throw here (locked file, missing parent dir) would exit with box = backup
    # key and PC = whatever it had, silently.
    try {
        $kdir = Split-Path -Parent $KeyFile
        if ($kdir -and -not (Test-Path -LiteralPath $kdir)) { New-Item -ItemType Directory -Path $kdir -Force | Out-Null }
        Copy-Item -LiteralPath $PcKeyBak -Destination $KeyFile -Force
    } catch {
        Fail 70 ("the device was RESTORED but the PC half was NOT: copying '{0}' -> '{1}' failed ({2}). The box now holds the backup key and this PC does not. Copy it by hand." -f $PcKeyBak, $KeyFile, $_.Exception.Message)
    }
    $fpLive = Get-FpFromBytes ([IO.File]::ReadAllBytes($KeyFile))
    $fpDev = Invoke-Box -RemoteCommand (Get-CmdSectorFp -Lba $KEY_LBA)
    if ($fpLive -ne $fpBak -or $fpDev.Out -ne $fpBak) {
        Fail 70 ("post-rollback mismatch: live PC {0}, device {1}, expected {2}{3}" -f $fpLive, $fpDev.Out, $fpBak, (Get-EmptyReadHint $fpDev.Out))
    }
    # W2-equivalent: the whole sector must match the backup file, not just its
    # key bytes -- the same standard -Rekey holds itself to.
    $rmd5 = Invoke-Box -RemoteCommand (Get-CmdSectorMd5 -Lba $KEY_LBA)
    if ($rmd5.Code -ne 0 -or $rmd5.Out -ne $bmd5.Out) {
        Fail 70 ("post-rollback whole-sector md5 '{0}' != the backup's '{1}' -- the key bytes match but the sector does not" -f $rmd5.Out, $bmd5.Out)
    }
    # Neighbours BEFORE the success line, so a rollback that damaged one cannot
    # print "restored and verified" above the damage report.
    [void](Test-Neighbours -Fatal)
    Pass ('both halves restored and verified -- fingerprint {0}, sector md5 {1}' -f $fpBak, $rmd5.Out)

    Say ''
    Say '  Start the receiver FRESH (it loads the key at startup), then boot JARVIS and'
    Say '  send one query. A coherent answer means both halves agree again.'
    Write-Transcript -Command ('rollback complete, fp={0}' -f $fpBak) -ExitCode 0
    exit 0
}

# ===========================================================================
# -Rekey
# ===========================================================================
Say '== jarvis_admin.ps1 -Rekey =='
$pf = Invoke-Preflight
if (-not $pf) { Fail 10 'preflight did not complete -- nothing was changed' }
$fpBox = $pf.FpBox

Say ''
Say '== neighbour sectors BEFORE the write (so "damaged" can be attributed) =='
Say '  (a neighbour already damaged HERE is pre-existing -- this script has not written anything yet)'
# Test-Neighbours calls Fail, which exits in -Rekey, so it aborts on its own.
# A wrapper `if (-not (Test-Neighbours)) { Fail ... }` looked like a second gate
# but was unreachable, and its better wording never reached the operator.
[void](Test-Neighbours)

# -- B1 / B2 : backups, verified by FINGERPRINT not size --------------------
Say ''
Say '== backups =='
Copy-Item -LiteralPath $KeyFile -Destination $PcKeyBak -Force
Write-Transcript -Command ('copy "{0}" -> "{1}"' -f $KeyFile, $PcKeyBak) -ExitCode 0
if (-not (Test-Path -LiteralPath $PcKeyBak)) { Fail 20 ("PC backup '{0}' was not created" -f $PcKeyBak) }
$bakLen = (Get-Item -LiteralPath $PcKeyBak).Length
if ($bakLen -ne [int]$KEY_LEN) { Fail 20 ("PC backup is {0} bytes, expected {1}" -f $bakLen, $KEY_LEN) }
$fpBak = Get-FpFromBytes ([IO.File]::ReadAllBytes($PcKeyBak))
if ($fpBak -ne $pf.FpPc) { Fail 20 ("PC backup fingerprint {0} != live {1} -- the backup is not a copy of what is live" -f $fpBak, $pf.FpPc) }
Pass ('B1 PC backup -> {0} ({1} B, fp {2} -- verified restorable, not merely present)' -f $PcKeyBak, $KEY_LEN, $fpBak)
Add-Artifact -Path $PcKeyBak -What 'PC backup of the PREVIOUS key (32 B)'

$b = Invoke-Box -RemoteCommand (Get-CmdBoxBackup -Name $BoxSlotBak)
if ($b.Code -ne 0) { Fail 21 ("box backup FAILED: {0}" -f $b.Out) }
$bs = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $BoxSlotBak)
if ($bs.Code -ne 0 -or [int]$bs.Out -ne $SLOT_BYTES) {
    Fail 21 ("box backup is '{0}' bytes, expected {1} (the SLOT is {1} B; 32 B is the KEY FILE -- the runbook conflates them)" -f $bs.Out, $SLOT_BYTES)
}
$bfp = Invoke-Box -RemoteCommand (Get-CmdBoxFileFp -Name $BoxSlotBak)
if ($bfp.Code -ne 0 -or $bfp.Out -ne $fpBox) {
    Fail 21 ("box backup fingerprint '{0}' != FP_BOX '{1}' -- a {2}-byte file that is not the slot would pass a size check, which is why this is fingerprinted" -f $bfp.Out, $fpBox, $SLOT_BYTES)
}
Pass ('B2 box backup -> ~/{0} ({1} B, fp {2})' -f $BoxSlotBak, $SLOT_BYTES, $bfp.Out)
Add-Artifact -Path ('{0}:~/{1}' -f $BoxHost, $BoxSlotBak) -What 'box backup of the PREVIOUS slot (512 B)'

# -- G1..G5 : generate into scratch -----------------------------------------
Say ''
Say '== generate (scratch only -- nothing live is touched yet) =='
$scratch = Join-Path $env:TEMP ('jarvis-rekey-{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
Add-Artifact -Path $scratch -What 'scratch dir: holds the new key file AND the new slot' -NewKey
$scratchKey  = Join-Path $scratch 'control_key.bin'
$scratchSlot = Join-Path $scratch 'jkey_slot.bin'

$genCmd = Get-CmdGen -ScratchDir $scratch
Info $genCmd
& py -3 $genKey --key-out $scratchKey --slot-out $scratchSlot | Out-Null
$genRc = $LASTEXITCODE
Write-Transcript -Command $genCmd -ExitCode $genRc
if ($genRc -ne 0) { Fail 30 ("gen_control_key.py exited {0} -- nothing live has been touched" -f $genRc) }

if (-not (Test-Path -LiteralPath $scratchKey)) { Fail 31 'the generator produced no key file' }
$newKeyBytes = [IO.File]::ReadAllBytes($scratchKey)
if ($newKeyBytes.Length -ne [int]$KEY_LEN) { Fail 31 ("new key is {0} bytes, expected {1}" -f $newKeyBytes.Length, $KEY_LEN) }
if (-not ($newKeyBytes | Where-Object { $_ -ne 0 })) { Fail 31 'the new key is all zero -- the box would treat that as no key' }
$fpNew = Get-FpFromBytes $newKeyBytes
Pass ('G2 new key {0} bytes, non-zero -- FP_NEW = {1}' -f $KEY_LEN, $fpNew)

if (-not (Test-Path -LiteralPath $scratchSlot)) { Fail 32 'the generator produced no slot image' }
$slotBytes = [IO.File]::ReadAllBytes($scratchSlot)
if ($slotBytes.Length -ne $SLOT_BYTES) { Fail 32 ("new slot is {0} bytes, expected {1}" -f $slotBytes.Length, $SLOT_BYTES) }
$slotMagic = [BitConverter]::ToUInt32($slotBytes, 0)
if ($slotMagic -ne [uint32]$KEY_MAGIC) { Fail 32 ("new slot magic 0x{0:X} != 0x{1:X}" -f $slotMagic, $KEY_MAGIC) }
$slotVer = [BitConverter]::ToUInt16($slotBytes, 4)
if ($slotVer -ne [uint16]$KEY_VERSION) { Fail 32 ("new slot version {0} != header {1}" -f $slotVer, $KEY_VERSION) }
if ($slotBytes[6] -ne 0 -or $slotBytes[7] -ne 0) { Fail 32 'new slot reserved0 is not zero' }
foreach ($z in @(@{o=40;n=8;f='seq_floor'}, @{o=48;n=4;f='boot_epoch'})) {
    for ($i = 0; $i -lt $z.n; $i++) {
        if ($slotBytes[$z.o + $i] -ne 0) { Fail 32 ("new slot {0} is not zero -- 6-5/M3-3 moved the replay floor OFF this sector on purpose" -f $z.f) }
    }
}
for ($i = 52; $i -lt $SLOT_BYTES; $i++) {
    if ($slotBytes[$i] -ne 0) { Fail 32 ("new slot has non-zero padding at offset {0}" -f $i) }
}
Pass ('G3 new slot {0} B: magic, version {1}, reserved0/seq_floor/boot_epoch and 52..{0} all zero' -f $SLOT_BYTES, $slotVer)

# -- G4 : the highest-value check -------------------------------------------
$slotKey = New-Object byte[] ([int]$KEY_LEN)
[Array]::Copy($slotBytes, $KEY_OFFSET, $slotKey, 0, [int]$KEY_LEN)
$fpSlot = Get-FpFromBytes $slotKey
if ($fpSlot -ne $fpNew) {
    Fail 33 ("G4 THE SLOT AND THE KEY FILE CARRY DIFFERENT KEYS (slot {0}, file {1}). Without this check, provisioning would APPEAR to succeed and the channel would be dead with no error anywhere until a query got no reply." -f $fpSlot, $fpNew)
}
Pass ('G4 the key inside the slot == the key file ({0})' -f $fpNew)

if ($fpNew -eq $fpBox) { Fail 34 'G5 the "new" key is identical to the current one -- refusing a no-op re-key' }
Pass ('G5 FP_NEW {0} != FP_BOX {1}' -f $fpNew, $fpBox)

# -- T1 / T2 : transfer, proven byte-for-byte -------------------------------
Say ''
Say '== transfer =='
$localMd5 = Get-FileMd5 -Path $scratchSlot
$scpPretty = Get-CmdScp -ScratchDir $scratch
Info $scpPretty
Push-Location $scratch
try {
    & scp 'jkey_slot.bin' ("{0}:{1}" -f $BoxHost, $BoxSlotNew) | Out-Null
    $scpRc = $LASTEXITCODE
} finally { Pop-Location }
Write-Transcript -Command $scpPretty -ExitCode $scpRc
if ($scpRc -ne 0) { Fail 40 ("scp exited {0} -- the box slot is UNCHANGED and both backups are in place" -f $scpRc) }

$remoteMd5 = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $BoxSlotNew)
if ($remoteMd5.Code -ne 0 -or $remoteMd5.Out -ne $localMd5) {
    Fail 41 ("T2 slot md5 differs across the wire: local {0}, box '{1}'. Not 'did 512 bytes arrive' -- this project has silently corrupted binary through a text channel three times." -f $localMd5, $remoteMd5.Out)
}
Add-Artifact -Path ('{0}:~/{1}' -f $BoxHost, $BoxSlotNew) -What 'scratch slot ON THE BOX' -NewKey
Pass ('T2 whole-slot md5 identical on both sides ({0})' -f $localMd5)

# -- the write --------------------------------------------------------------
Say ''
Say '=============================== THE WRITE ==================================='
Say ('  target      : {0} LBA {1} (parsed from control_key.h, not typed here)' -f $BoxDevice, $KEY_LBA)
Say ('  fingerprint : {0}  ->  {1}' -f $fpBox, $fpNew)
Say ('  PC backup   : {0}' -f $PcKeyBak)
Say ('  box backup  : ~/{0}' -f $BoxSlotBak)
Say ('  command     : {0}' -f (Get-CmdBoxWrite -Name $BoxSlotNew))
Say '============================================================================='
if (-not $Yes) {
    $answer = Read-Host ("  type {0} to write, anything else to abort" -f $ConfirmWord)
    # -ceq: string comparison in PowerShell is case-INSENSITIVE by default, and a
    # confirmation that accepts 'rekey-write-now' is not the confirmation that
    # was asked for.
    if (-not ("$answer".Trim() -ceq $ConfirmWord)) {
        Say '  not confirmed -- NOTHING was written. Both backups are in place.'
        Say ('  the scratch slot is still on the box at ~/{0}; remove it with:' -f $BoxSlotNew)
        Say ('    ssh {0} ''{1}''' -f $BoxHost, (Get-CmdBoxRemove -Name $BoxSlotNew))
        Write-Transcript -Command 'write NOT confirmed' -ExitCode 50
        exit 50
    }
} else {
    Warn '-Yes: typed confirmation SKIPPED, and the redirected-stdin refusal with it. No verification gate is skipped, but this write can now proceed with no human present.'
}

$w = Invoke-Box -RemoteCommand (Get-CmdBoxWrite -Name $BoxSlotNew)
if ($w.Code -ne 0) { Fail 51 ("the dd write FAILED: {0}" -f $w.Out) }
$script:keyIsLive = $true    # from here on, the scratch copies hold the LIVE key
Pass 'write completed (conv=fsync)'

# -- W1 / W2 / W3 : verify from the DEVICE ----------------------------------
Say ''
Say '== post-write verification (read back from the device, not from the file) =='
$fpDev = Invoke-Box -RemoteCommand (Get-CmdSectorFp -Lba $KEY_LBA)
if ($fpDev.Code -ne 0 -or $fpDev.Out -ne $fpNew) {
    Fail 60 ("W1 on-device fingerprint '{0}' != FP_NEW '{1}'{2} -- roll back: jarvis_admin.bat -Rollback" -f $fpDev.Out, $fpNew, (Get-EmptyReadHint $fpDev.Out))
}
Pass ('W1 on-device key fingerprint == FP_NEW ({0})' -f $fpNew)

$devMd5 = Invoke-Box -RemoteCommand (Get-CmdSectorMd5 -Lba $KEY_LBA)
if ($devMd5.Code -ne 0 -or $devMd5.Out -ne $localMd5) {
    Fail 61 ("W2 on-device sector md5 '{0}' != local slot md5 '{1}'" -f $devMd5.Out, $localMd5)
}
Pass ('W2 whole-sector md5 matches the local slot ({0})' -f $localMd5)

[void](Test-Neighbours -Fatal)

# -- S1 / S2 : swap the live PC half ----------------------------------------
Say ''
Say '== swap the Main-PC half =='
Copy-Item -LiteralPath $scratchKey -Destination $KeyFile -Force
Write-Transcript -Command ('copy scratch key -> "{0}"' -f $KeyFile) -ExitCode 0
if (-not (Test-Path -LiteralPath $KeyFile)) { Fail 70 ("S1 '{0}' is missing after the copy" -f $KeyFile) }
if ((Get-Item -LiteralPath $KeyFile).Length -ne [int]$KEY_LEN) { Fail 70 'S1 the live key file is not the right size after the copy' }
$fpLive = Get-FpFromBytes ([IO.File]::ReadAllBytes($KeyFile))
# Re-read the device HERE rather than reusing W1's $fpDev: W1 already aborted
# unless that value equalled $fpNew, so reusing it made one third of this
# "three-way" check unreachable -- a term that can never fire is not a check.
$fpDev2 = Invoke-Box -RemoteCommand (Get-CmdSectorFp -Lba $KEY_LBA)
if ($fpDev2.Code -ne 0) { Fail 70 ("S2 could not re-read the device (exit {0}) -- the swap is NOT verified" -f $fpDev2.Code) }
if ($fpLive -ne $fpNew -or $fpDev2.Out -ne $fpNew) {
    Fail 70 ("S2 three-way mismatch: live PC {0}, device {1}, expected {2}{3}" -f $fpLive, $fpDev2.Out, $fpNew, (Get-EmptyReadHint $fpDev2.Out))
}
Pass ('S2 live PC key == device key == FP_NEW ({0})' -f $fpNew)

# -- cleanup : only now, because until S2 passed the backups were the way back
Say ''
Say '== cleanup =='
$rm = Invoke-Box -RemoteCommand (Get-CmdBoxRemove -Name $BoxSlotNew)
# rm -f exits 0 for a file that never existed, so its exit code is not evidence
# of removal. Prove absence instead.
$rmGone = Invoke-Box -RemoteCommand ('test ! -e $HOME/{0}' -f $BoxSlotNew)
if ($rm.Code -eq 0 -and $rmGone.Code -eq 0) {
    Pass ('removed ~/{0} from the box -- it was a plaintext copy of the LIVE key, and the key is supposed to exist in exactly two places: the JKEY sector and this PC' -f $BoxSlotNew)
} else {
    Warn ('could not remove ~/{0} from the box -- DELETE IT BY HAND: it holds the live key' -f $BoxSlotNew)
}
try { Remove-Item -LiteralPath $scratch -Recurse -Force; Pass ('removed the scratch dir {0} (it held the live key twice)' -f $scratch) }
catch { Warn ('could not remove the scratch dir {0} -- it holds the live key; delete it by hand' -f $scratch) }

if ($KeepBackups) {
    Info ('-KeepBackups: the OLD pair is retained at "{0}" and ~/{1}' -f $PcKeyBak, $BoxSlotBak)
    Info 'they are inert (the box no longer accepts that key, so a frame signed with it is dropped at HMAC) but they are still key material -- delete them when the cooling-off period is over'
} else {
    try { Remove-Item -LiteralPath $PcKeyBak -Force; Pass ('removed {0} (old key)' -f $PcKeyBak) }
    catch { Warn ('could not remove {0}' -f $PcKeyBak) }
    $rmb = Invoke-Box -RemoteCommand (Get-CmdBoxRemove -Name $BoxSlotBak)
    $rmbGone = Invoke-Box -RemoteCommand ('test ! -e $HOME/{0}' -f $BoxSlotBak)
    if ($rmb.Code -eq 0 -and $rmbGone.Code -eq 0) { Pass ('removed ~/{0} from the box (old key) -- proven gone' -f $BoxSlotBak) }
    else { Warn ('could not remove ~/{0} from the box' -f $BoxSlotBak) }
    Info 'the old pair is gone, so -Rollback can no longer restore it. That is the default on purpose: the new pair is verified on both halves, and the box''s own JKEY sector is itself a recovery path for the PC half.'
}
Info 'note: rm UNLINKS, it does not securely erase -- and on an SSD, overwriting would not guarantee erasure either. No shred is implied.'

# -- what the operator does next (printed as instructions, never run) -------
Say ''
Say '========================= RE-KEY COMPLETE -- NEXT STEPS ======================'
Say ('  fingerprint now live on both halves: {0}   (was {1})' -f $fpNew, $fpBox)
Say ''
Say '  1. START THE RECEIVER FRESH. It loads the key ONCE at startup, so any'
Say '     receiver still running from before holds the OLD key.'
Say '       phasec\scripts\start_receiver.bat'
Say ''
Say '  2. Boot JARVIS one-shot and expect the CONTROL_IN telemetry flag. That flag'
Say '     requires g_ctrl_key_ok, so its presence proves the box read and accepted'
Say '     the new slot.'
Say ('       ssh {0} "sudo efibootmgr --bootnext 0000 && sudo reboot"' -f $BoxHost)
Say ''
Say '  3. Send ONE query from the console. A coherent answer means both halves'
Say '     agree.'
Say ''
Say '  4. THE FAILURE SIGNATURE IS NO REPLY AT ALL, NOT AN ERROR. A key mismatch is'
Say '     dropped at HMAC and the box never answers -- which looks exactly like a'
Say '     dead box. If that happens, it is the key, not the box.'
Say ''
if ($KeepBackups) {
    Say ('  5. Roll back with:  phasec\scripts\jarvis_admin.bat -Rollback')
} else {
    Say ('  5. Rollback is no longer available (the old pair was deleted -- pass')
    Say ('     -KeepBackups next time to retain it). To recover the PC half from the')
    Say ('     box, read the JKEY sector''s key bytes back off {0}.' -f $BoxDevice)
}
Say '============================================================================='
Write-Transcript -Command ('rekey complete, {0} -> {1}' -f $fpBox, $fpNew) -ExitCode 0
exit 0
