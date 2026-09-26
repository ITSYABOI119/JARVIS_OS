<#
.SYNOPSIS
  JARVIS admin -- the DESTRUCTIVE half of the operator tooling. v3 scope: the
  control-IN re-key, and the JSEM projection write (-Project / -ProjectRestore,
  Phase 7 MS3b). Separate from jarvis_menu.ps1 on purpose; see below.

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
                Writes no device and generates no key; its JSEM legs stage probe
                files in the box's home and remove them. Safe at any time.
    -Rekey      the full procedure, behind a typed confirmation before the write.
    -Rollback   restore both halves from the .BAK pair, then verify.
    -Project    write a JSEM projection image (built beforehand by the memory
                store's `project` verb) to the box's semantic-store region, behind
                a typed PROJECT-WRITE-NOW. With -DryRun it runs every gate up to
                the prompt, prints the plan, removes what it staged and exits 0
                without prompting or writing.
    -ProjectRestore
                write a retained pre-image back over the region, behind a typed
                PROJECT-RESTORE-NOW. -Rollback is key-specific and is not reused.

  THE JSEM WRITE, IN ONE PARAGRAPH (the rules are the memory design's section 9,
  MS3b refined 2026-09-26). The write never builds: the image must be exactly the
  region's size, carry the manifest's three md5s AND equal an expected image md5
  the operator names, since only that last check pins WHICH household is written.
  The region's current content must equal an expected pre-image md5, or the mode
  refuses -- an unexpected region is the operator's decision, never a thing to
  overwrite. The whole region is copied to a pre-image on the box and on this PC
  first. The records are written first and the header last, both with
  conv=fsync,notrunc, then sync, drop_caches, and a size-checked iflag=direct
  readback whose three md5s must equal the manifest's. Three anchors (the
  episodic header and tail, the JACT head) are read before and must be unchanged
  after. The readback is parsed on the box by parse_semantic.py. Every LBA, count
  and magic is parsed from the firmware headers.

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
  means a -Rekey could proceed right now AND every JSEM projection leg passed.
  Those legs run every shape -Project and -ProjectRestore use except the two
  device writes: the two-part write rehearsed into a FILE on the box, the header
  and records md5 slices of that file, the pull of a file back to this PC, the
  region and anchor reads, the range-to-file read, drop_caches, cmp, the box
  clone's parser check, the local gates L1-L3, and parse_semantic.py over the
  verified image (or, with no valid local image, over a copy of the region).
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
  -Yes is REFUSED with -Project and -ProjectRestore (exit 3): the JSEM
  pre-registration says the operator types the confirmation.
.PARAMETER Project
  Write the JSEM projection image to the box's semantic-store region. Every gate
  is a hard abort; the write sits behind a typed PROJECT-WRITE-NOW.
.PARAMETER ProjectRestore
  Write the retained pre-image (-PreImage) back over the region, with the same
  readback and anchor discipline, behind a typed PROJECT-RESTORE-NOW.
.PARAMETER DryRun
  Valid only with -Project (exit 3 otherwise). Runs gates 1-10 exactly as the real
  run does, prints the plan with both write commands verbatim, removes the staged
  image and the pre-image from both hosts, and exits 0. Never prompts, never
  writes the device.
.PARAMETER Image
  The projection image. Default %USERPROFILE%\.jarvis\memory\exports\jsem.img.
.PARAMETER Manifest
  The manifest the same `project` run wrote beside the image. Default
  %USERPROFILE%\.jarvis\memory\exports\jsem.manifest.json.
.PARAMETER ExpectPreMd5
  The md5 the region must hold before -Project writes it. Default
  1365c929581e56c8bbc59308feeab8e3, the all-zero region; a later re-projection
  passes the previous image's md5.
.PARAMETER ExpectImageMd5
  The md5 the image must have. Default 10e4e0fe6ff9be360b14bb75ce68d039, the
  synthetic household. This is the one check that pins which household is
  written: an image and its manifest can never disagree with each other.
.PARAMETER PreImage
  -ProjectRestore's source. Empty means the newest jsem_pre_<UTC stamp>.bin in
  %USERPROFILE%\.jarvis\admin\, chosen by the stamp in its name. Its same-named
  copy must also exist in the box's home directory.
.PARAMETER BoxRepo
  The repo clone ON THE BOX, where parse_semantic.py lives. Default the literal
  $HOME/Desktop/JARVIS_OS, single-quoted so that $HOME expands on the box, not
  here (a tilde is avoided: it is not expanded after '=' by every shell).

.NOTES
  Exit codes:
    0  ok
    2  no mode given (usage printed)
    3  more than one mode given, or -Yes / -DryRun used where they are refused
    4  stdin redirected (see -Check)
    5  a prerequisite is missing, or a -Check / -DryRun shape or its cleanup failed
    6  a header constant could not be derived
    7  the box clone is stale, dirty in the parser, or lacks it
    8  interrupted inside the JSEM write window (the device may hold part of the
       image; see THE JSEM WRITE WINDOW below)
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
   90  local image or manifest invalid, or not the expected image
   91  local image header invalid        92  JARVIS_SEMANTIC is not 0
   93  pre-image backup unverified
   94  the region is not the expected pre-image, or could not be read
   95  drop_caches failed                96  readback size wrong
   97  restore preconditions             98  restore readback mismatch
   99  parse count != manifest n
  -Project and -ProjectRestore reuse 10 (box unreachable), 40/41 (transfer),
  50 (not confirmed), 51 (dd write failed), 61 (readback md5 mismatch) and 62
  (an anchor unreadable or changed -- act immediately) in those meanings.

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

  THE JSEM WRITE WINDOW. From the first JSEM write until R2 passes, the region
  may hold part of the image. Every FAIL in that window BEFORE the anchor check
  re-reads the three anchors: a changed one exits 62; a failed re-read is
  reported as a READ failure, keeps the gate's own code, and is never called
  damage. The anchor check itself (gate 17, and the restore's after-check) exits
  62 for an anchor that is changed OR unreadable, as the exit-code table says.
  An interruption exits 8. Every exit from the window prints the restore
  command -- a Ctrl+C, a dropped or hung ssh and an unhandled PowerShell error
  included; those exits read no anchor, because the connection may be the thing
  that failed. The pre-image on both hosts is the way back.
#>
[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$Rekey,
    [switch]$Rollback,
    [switch]$Project,
    [switch]$ProjectRestore,
    [switch]$DryRun,
    [switch]$KeepBackups,
    [switch]$Yes,
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.jarvis\control_key.bin'),
    [string]$BoxHost = 'jarvis',
    [int]$Port = 8800,
    [string]$Image = (Join-Path $env:USERPROFILE '.jarvis\memory\exports\jsem.img'),
    [string]$Manifest = (Join-Path $env:USERPROFILE '.jarvis\memory\exports\jsem.manifest.json'),
    [string]$ExpectPreMd5 = '1365c929581e56c8bbc59308feeab8e3',
    [string]$ExpectImageMd5 = '10e4e0fe6ff9be360b14bb75ce68d039',
    [string]$PreImage = '',
    [string]$BoxRepo = '$HOME/Desktop/JARVIS_OS'
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
$ProjectWord = 'PROJECT-WRITE-NOW'
$ProjectRestoreWord = 'PROJECT-RESTORE-NOW'
# The commit that added phase3/scripts/parse_semantic.py. The box clone must hold
# it as an ancestor of HEAD, because -Project parses its readback with that file.
$ParserCommit = '848e8a5'

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
# The JSEM write window (see .NOTES). Every variable Fail's post-write hook or a
# JSEM block's finally reads is initialised HERE, at the top: under StrictMode 2.0
# reading an unset variable throws, which would turn any Fail -- in -Rekey and
# -Rollback too -- into exit 1.
$script:jsemWritten = $false        # set true IMMEDIATELY before the first JSEM device write, nowhere else
$script:jsemInAnchorCheck = $false  # set only by Fail's post-write hook, around its anchor re-read
$script:jsemBannerShown = $false    # the WRITTEN banner prints once per run
$script:jsemInnerCode = 0           # the highest code a Fail raised inside the hook's re-read
$script:jsemBlockDone = $false      # a JSEM block's try reached its end
$script:jsemA0 = $null              # the anchors before the write
$script:jsemL = $null               # the layout
$script:jsemMode = ''               # 'project' or 'restore'
$script:jsemLocalPre = ''           # the pre-image (or restore source) on this PC
$script:jsemLeaf = ''               # its leaf, the same name on the box
function Add-Artifact {
    # -NewKey marks an artifact holding the NEWLY GENERATED key. Whether that key
    # is LIVE depends on whether the write has happened yet, which is why it is
    # resolved at print time rather than at registration.
    # -Region marks a copy of the JSEM region as it was BEFORE a -Project run --
    # the pre-image, which is -ProjectRestore's source and must be kept.
    # -Staged marks a file staged in the box's home (-Name is its leaf there), so
    # an abort at its own check still names it.
    param([string]$Path, [string]$What, [switch]$NewKey, [switch]$Region, [switch]$Staged, [string]$Name = '')
    $script:artifacts += [pscustomobject]@{ Path = $Path; What = $What; NewKey = [bool]$NewKey; Region = [bool]$Region; Staged = [bool]$Staged; Name = $Name }
}

function Remove-Artifact {
    # Once a cleanup PROVES a file gone, it is no longer an artifact to name.
    param([string]$Path)
    $script:artifacts = @($script:artifacts | Where-Object { $_.Path -ne $Path })
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
        $tag = if ($a.Region) { '   (the region as it was before this run -- your restore path for -ProjectRestore; keep it)' }
               elseif ($a.Staged) { ('   (staged copy on the box -- remove after inspection: ssh {0} rm -f ~/{1})' -f $BoxHost, $a.Name) }
               elseif ($a.NewKey -and $script:keyIsLive) { '   *** HOLDS THE LIVE KEY -- DELETE IT ***' }
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
    # Inside the post-write hook's own anchor re-read: record the code, never exit.
    # Only the hook sets this guard.
    if ($script:jsemInAnchorCheck) {
        if ($Code -gt $script:jsemInnerCode) { $script:jsemInnerCode = $Code }
        return
    }
    # THE POST-WRITE HOOK. Once the first JSEM write has been sent, a gate that
    # fails must not exit before the anchors are re-read: the readback runs before
    # the anchor check (the design's section 9, MS3b refined 2026-09-26), so a write
    # that damaged a neighbour would otherwise exit on R2 and never report it. The
    # anchor check's own verdict (62) is not re-read. The locals are set HERE: under
    # StrictMode an unset one throws, and a caller's own $ok must never leak in.
    if (-not $Check -and $script:jsemWritten) {
        $aA = $null
        $ok = $null
        $how = 'not re-read (code 62)'
        if ($Code -ne 62) {
            $script:jsemInAnchorCheck = $true
            $script:jsemInnerCode = 0
            try {
                Say '  -- the write has started: the three anchors are re-read before this exits --'
                $aA = Read-JsemAnchors -L $script:jsemL -ReadFailCode $Code -When 'after'
                if ($null -ne $aA) { $ok = Compare-JsemAnchors -Before $script:jsemA0 -After $aA }
            } finally {
                $script:jsemInAnchorCheck = $false
            }
        }
        $final = $Code
        if ($Code -eq 62) {
            $final = 62
        } elseif (($null -ne $ok -and -not $ok) -or $script:jsemInnerCode -eq 62) {
            $final = 62
            $how = 'changed'
        } elseif ($null -eq $aA) {
            Warn 'the anchors could not be READ after the write -- the sector is NOT proven changed; the original code stands'
            $how = 'unreadable'
        } else {
            $how = 'unchanged'
        }
        Show-JsemWrittenBanner
        Write-Transcript -Command ('ABORT: {0} [anchors after the write: {1}]' -f $Message, $how) -ExitCode $final
        Show-Artifacts
        exit $final
    }
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
$modeCount = @($Check, $Rekey, $Rollback, $Project, $ProjectRestore | Where-Object { $_ }).Count
# -DryRun belongs to -Project alone, and -Yes never reaches a JSEM write: the
# pre-registration says the operator TYPES that confirmation, and a -Yes that fell
# through a missing exit would skip REKEY-WRITE-NOW as well. Both refusals are
# checked before the menu so that a misuse is refused rather than opening it.
if ($DryRun -and -not $Project) {
    Write-Host '  FAIL: -DryRun is valid only with -Project' -ForegroundColor Red
    Write-Transcript -Command 'refused: -DryRun without -Project' -ExitCode 3
    exit 3
}
if ($Yes -and ($Project -or $ProjectRestore)) {
    Write-Host '  FAIL: -Yes is refused with -Project and -ProjectRestore -- the JSEM write is confirmed by typing, never skipped' -ForegroundColor Red
    Write-Transcript -Command 'refused: -Yes with a JSEM mode' -ExitCode 3
    exit 3
}
$MenuMode = $false
if ($modeCount -eq 0) {
    # A MENU IS COMPATIBLE WITH "never act on a bare invocation", because a menu
    # ASKS. Nothing here runs until an option is chosen, and every write still
    # goes through the typed REKEY-WRITE-NOW in the mode that performs it. What
    # changed is only how the tool is REACHED: every invocation until now needed
    # a fully-typed absolute path plus flags, and that produced three consecutive
    # shell failures in a row (a .bat is not on Git Bash's PATH, backslashes are
    # escapes in bash, and py cannot parse batch).
    if ([Console]::IsInputRedirected) {
        # Read-Host returns EMPTY IMMEDIATELY under a redirect rather than
        # blocking -- jarvis_menu.ps1 shipped an infinite loop from exactly this
        # (~10,000 iterations in three minutes). Refuse before the loop exists.
        Write-Host '  FAIL: the menu needs a real console -- stdin is redirected here, so no choice can be read.' -ForegroundColor Red
        Write-Host '        Double-click jarvis_admin.bat, or pass a mode explicitly:' -ForegroundColor Red
        Write-Host '          jarvis_admin.bat -Check     (validates everything, runs nothing)' -ForegroundColor Red
        Write-Transcript -Command 'menu refused: stdin redirected' -ExitCode 4
        exit 4
    }
    $MenuMode = $true
    $script:mode = 'menu'
}
if ($modeCount -gt 1) {
    Write-Host '  FAIL: -Check, -Rekey, -Rollback, -Project and -ProjectRestore are mutually exclusive -- pick one' -ForegroundColor Red
    Write-Transcript -Command 'more than one mode given' -ExitCode 3
    exit 3
}
if (-not $MenuMode) {
    $script:mode = if ($Check) { 'check' } elseif ($Rekey) { 'rekey' }
                   elseif ($Project) { 'project' } elseif ($ProjectRestore) { 'projectrestore' }
                   else { 'rollback' }
}

# The confirmed modes prompt. With stdin redirected Read-Host returns empty
# immediately rather than blocking (jarvis_menu.ps1 shipped an infinite loop from
# exactly this), and an empty answer must never be able to satisfy a
# confirmation. Refuse up front and name the mode that IS non-interactive.
# -Project -DryRun is the one other non-interactive mode: it never reaches a prompt.
if (-not $MenuMode -and -not $Check -and -not ($Project -and $DryRun) -and -not $Yes -and [Console]::IsInputRedirected) {
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
# JSEM projection (Phase 7 MS3b): the layout and the command builders. Every
# function in this section is PURE -- it takes every value as a parameter and
# reads no script-scope variable -- so phasec/scripts/test_jarvis_admin_jsem.ps1
# can extract it by AST and run it under pwsh in CI. That test also asserts the
# golden strings, the write plan's order and that no JSEM write bypasses the plan.
# No builder contains a double quote (see the note above), every md5 pipeline
# starts `set -o pipefail;`, and every dd carries bs=512.
# ---------------------------------------------------------------------------
$hdrSem   = Join-Path $repoRoot 'phase3\src\ai\semantic_store.h'
$hdrEpi   = Join-Path $repoRoot 'phase3\src\ai\episodic_store.h'
$hdrAct   = Join-Path $repoRoot 'phase3\src\ai\action_audit.h'
$hdrDebug = Join-Path $repoRoot 'phase3\src\sel4\jarvis_debug.h'

function Get-JsemLayout {
    # The region and its three anchors, every value derived through Get-HeaderConst
    # and Get-LeMagicHex -- the same parser the rest of this script uses -- never a
    # regex of its own and never a typed number. The region is the header sector
    # plus MAX_FACTS record slots; the episodic tail is that store's LAST 16 record
    # slots (records start at base + 1); the JACT head is its header plus 15 slots.
    param([string]$SemHeader, [string]$EpiHeader, [string]$ActHeader)
    $base     = Get-HeaderConst -Path $SemHeader -Name 'SEM_STORE_BASE_LBA'
    $max      = Get-HeaderConst -Path $SemHeader -Name 'SEM_STORE_MAX_FACTS'
    $semMagic = Get-HeaderConst -Path $SemHeader -Name 'SEM_STORE_MAGIC'
    $semVer   = Get-HeaderConst -Path $SemHeader -Name 'SEM_STORE_VERSION'
    $epiBase  = Get-HeaderConst -Path $EpiHeader -Name 'EPI_STORE_BASE_LBA'
    $epiMax   = Get-HeaderConst -Path $EpiHeader -Name 'EPI_STORE_MAX_ENTRIES'
    $epiMagic = Get-HeaderConst -Path $EpiHeader -Name 'EPI_STORE_MAGIC'
    $actBase  = Get-HeaderConst -Path $ActHeader -Name 'ACT_AUDIT_BASE_LBA'
    $actMagic = Get-HeaderConst -Path $ActHeader -Name 'ACT_AUDIT_MAGIC'
    $anchor   = [uint64]16
    [pscustomobject]@{
        BaseLba      = [uint64]$base
        MaxFacts     = [uint64]$max
        RecordsLba   = [uint64]($base + 1)
        RegionCount  = [uint64]($max + 1)
        RegionBytes  = [uint64](($max + 1) * 512)
        SemMagic     = [uint64]$semMagic
        SemMagicHex  = (Get-LeMagicHex $semMagic)
        SemVersion   = [uint64]$semVer
        EpiHeaderLba = [uint64]$epiBase
        EpiTailLba   = [uint64]($epiBase + 1 + $epiMax - $anchor)
        EpiMagicHex  = (Get-LeMagicHex $epiMagic)
        ActHeadLba   = [uint64]$actBase
        ActMagicHex  = (Get-LeMagicHex $actMagic)
        AnchorCount  = $anchor
    }
}

function Get-CmdRangeMd5 {
    # A device range hashed on the box. Beware: a SHORT read hashes as if whole in
    # this shape, so a mismatch here cannot tell "unexpected content" from "short
    # read" -- Get-CmdRangeToFile plus a size check is the shape that can.
    param([string]$Device, [uint64]$Lba, [uint64]$Count)
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count={2} iflag=direct status=none | md5sum | cut -c1-32' -f $Device, $Lba, $Count
}

function Get-CmdRangeToFile {
    # The login shell owns the redirect, so the file is owned by the ssh user.
    param([string]$Device, [uint64]$Lba, [uint64]$Count, [string]$Name)
    'set -o pipefail; sudo -n dd if={0} bs=512 skip={1} count={2} iflag=direct status=none > $HOME/{3}' -f $Device, $Lba, $Count, $Name
}

function Get-CmdImageWrite {
    # notrunc IS LOAD-BEARING: without it GNU dd truncates a FILE target, so the
    # header write (seek 0 in -Check's rehearsal) would destroy the records written
    # just before it -- measured on the box at coreutils 9.4, the file ends 512
    # bytes long. On the device it changes nothing (a block device is not
    # truncated), so ONE builder serves the rehearsal and the real write.
    param([string]$Device, [string]$Name, [uint64]$InSkip, [uint64]$Seek, [uint64]$Count)
    'sudo -n dd if=$HOME/{0} of={1} bs=512 skip={2} seek={3} count={4} conv=fsync,notrunc status=none' -f $Name, $Device, $InSkip, $Seek, $Count
}

function Get-JsemWritePlan {
    # EVERY JSEM write goes through here: the records first, the header last, so a
    # write interrupted between them leaves a header the box still reads as the
    # OLD region's rather than a new header over old records. Header-first would
    # also pass -Check's file rehearsal without notrunc, which is why the order is
    # pinned by the unit test and must never be swapped to make anything green.
    param([string]$Device, [string]$Name, [uint64]$BaseLba, [uint64]$MaxFacts)
    Get-CmdImageWrite -Device $Device -Name $Name -InSkip 1 -Seek ($BaseLba + 1) -Count $MaxFacts
    Get-CmdImageWrite -Device $Device -Name $Name -InSkip 0 -Seek $BaseLba -Count 1
}

function Get-CmdDropCaches {
    # No redirect, no pipe, no quote: `echo 3 > /proc/...` FAILS here because the
    # shell doing the redirect is not root. drop_caches frees only CLEAN pages, so
    # sync comes first; the iflag=direct read is what carries the guarantee.
    'sync; sudo -n sysctl -q vm.drop_caches=3'
}

function Get-CmdFileSliceMd5 {
    param([string]$Name, [string]$Part)
    switch ($Part) {
        'header'  { return ('set -o pipefail; head -c 512 $HOME/{0} | md5sum | cut -c1-32' -f $Name) }
        'records' { return ('set -o pipefail; tail -c +513 $HOME/{0} | md5sum | cut -c1-32' -f $Name) }
        'whole'   { return (Get-CmdBoxFileMd5 -Name $Name) }
    }
    throw ("unknown slice '{0}' (header, records or whole)" -f $Part)
}

function Get-CmdCmpHead {
    # A mismatch is EXPECTED to exit non-zero, so there is deliberately no pipefail.
    param([string]$A, [string]$B)
    'cmp -l $HOME/{0} $HOME/{1} | head -n 20' -f $A, $B
}

function Get-CmdParseSemantic {
    param([string]$BoxRepo, [string]$Name)
    'set -o pipefail; python3 {0}/phase3/scripts/parse_semantic.py --json $HOME/{1}' -f $BoxRepo, $Name
}

function Get-CmdScpTo {
    # DISPLAY ONLY, like Get-CmdScp: the transfer runs as Push-Location then scp
    # with a BARE file name, because a Windows path makes scp read C as a host.
    param([string]$BoxHost, [string]$Dir, [string]$LocalName, [string]$RemoteName)
    'cd {0}; scp {1} {2}:{3}' -f $Dir, $LocalName, $BoxHost, $RemoteName
}

function Get-CmdScpFrom {
    # DISPLAY ONLY, as above.
    param([string]$BoxHost, [string]$Dir, [string]$RemoteName, [string]$LocalName)
    'cd {0}; scp {1}:{2} {3}' -f $Dir, $BoxHost, $RemoteName, $LocalName
}

function Get-ForwardArgs {
    # What the chooser forwards to a child: -Name, value pairs, in the map's order,
    # for every NON-EMPTY value. Empty values are dropped because an empty string
    # vanishes as a native argument, so `-PreImage ''` would reach the child as a
    # bare -PreImage and abort it with "Missing an argument". A value holding a
    # space stays ONE element.
    param([System.Collections.IDictionary]$Params)
    $out = @()
    foreach ($k in $Params.Keys) {
        $v = [string]$Params[$k]
        if ($v -ne '') {
            $out += ('-{0}' -f $k)
            $out += $v
        }
    }
    $out
}

function Get-BytesMd5 {
    param([byte[]]$Bytes, [int]$Offset, [int]$Count)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try { $h = $md5.ComputeHash($Bytes, $Offset, $Count) } finally { $md5.Dispose() }
    (($h | ForEach-Object { $_.ToString('x2') }) -join '')
}

function Get-JsemWrittenBanner {
    # What every exit from the JSEM write window prints: the region may now hold
    # part of the image, where the way back is, and the exact command to take it.
    param([string]$Mode, [string]$LocalPre, [string]$BoxHost, [string]$Leaf)
    switch ($Mode) {
        'project' { return ('THE JSEM REGION HAS BEEN WRITTEN, possibly only in part. The pre-image is kept at {0} and {1}:~/{2}. Restore it with: jarvis_admin.bat -ProjectRestore -PreImage {0}' -f $LocalPre, $BoxHost, $Leaf) }
        'restore' { return ('THE RESTORE WRITE HAS STARTED, and the region may hold a mix of old and new bytes. The source is still at {0} and {1}:~/{2}. Re-run: jarvis_admin.bat -ProjectRestore -PreImage {0}' -f $LocalPre, $BoxHost, $Leaf) }
    }
    throw ("unknown mode '{0}' (project or restore)" -f $Mode)
}

function Test-JsemPreImageName {
    # The leaf is interpolated into root commands on the box, so ONLY the shape
    # -Project writes is accepted: case-sensitive, ASCII digits, and \z rather than
    # $ because .NET's $ also matches before a trailing newline.
    param([string]$Leaf)
    return ($Leaf -cmatch '^jsem_pre_[0-9]{8}T[0-9]{6}Z\.bin\z')
}

function ConvertTo-JsemAnchorLines {
    # The sidecar beside a local pre-image: ASCII name=value lines, in order. A name
    # holds spaces but never '=', so a reader splits on the FIRST '='.
    param([System.Collections.IDictionary]$Values)
    $out = @()
    foreach ($k in $Values.Keys) {
        $line = '{0}={1}' -f $k, $Values[$k]
        if ("$k".Contains('=') -or $line -match '[\r\n]' -or $line -match '[^\x20-\x7E]') {
            throw ("sidecar entry '{0}' is not a printable-ASCII name without '=' and a one-line value" -f $k)
        }
        $out += $line
    }
    $out
}

function ConvertFrom-JsemAnchorLines {
    param([string[]]$Lines)
    $vals = [ordered]@{}
    foreach ($ln in $Lines) {
        if ($ln -eq '') { continue }
        $i = $ln.IndexOf('=')
        if ($i -lt 1) { throw ("sidecar line '{0}' is not name=value" -f $ln) }
        $vals[$ln.Substring(0, $i)] = $ln.Substring($i + 1)
    }
    $vals
}

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

# ---------------------------------------------------------------------------
# JSEM projection runtime helpers (-Project, -ProjectRestore, and -Check's own
# projection section). These DO read script state and reach the box; what they
# run on the box is always one of the pure builders above.
# ---------------------------------------------------------------------------
function Get-JsemLayoutChecked {
    # Gate 1's constants. A failed parse is exit 6: Fail exits outside -Check; in
    # -Check it accumulates, and this returns $null so the caller skips the section.
    try {
        $l = Get-JsemLayout -SemHeader $hdrSem -EpiHeader $hdrEpi -ActHeader $hdrAct
    } catch {
        Fail 6 ('{0} -- every JSEM LBA, count and magic is parsed from the firmware headers, never typed' -f $_.Exception.Message)
        return $null
    }
    Pass ('JSEM region LBA {0} for {1} sectors ({2} bytes), records from LBA {3}; anchors: episodic header {4}, episodic tail {5} for {6}, JACT head {7} for {6} -- all parsed from the headers' -f `
          $l.BaseLba, $l.RegionCount, $l.RegionBytes, $l.RecordsLba, $l.EpiHeaderLba, $l.EpiTailLba, $l.AnchorCount, $l.ActHeadLba)
    return $l
}

function Test-JsemTools {
    $ok = $true
    foreach ($t in @('ssh', 'scp')) {
        if (Get-Command $t -ErrorAction SilentlyContinue) { Pass ('{0} present' -f $t) }
        else { Fail 5 ('{0} not found on PATH -- it ships with the Windows OpenSSH client' -f $t); $ok = $false }
    }
    return $ok
}

function Test-JsemBoxReachable {
    $probe = Invoke-Box -RemoteCommand 'true'
    if ($probe.Code -ne 0) {
        Fail 10 ("ssh to '{0}' failed. THE BOX MUST BE ON UBUNTU: JARVIS has no sshd, and the region must never be written under a running box. Do not guess -- check the machine." -f $BoxHost)
        return $false
    }
    Pass ("P1 box reachable over ssh ('{0}' answered) -- it is on Ubuntu" -f $BoxHost)
    return $true
}

function Read-JsemAnchors {
    # The three anchors, read with the SAME shapes before and after a write. A READ
    # failure is reported apart from a VALUE, exactly as Test-Neighbours does,
    # because a transient ssh/sudo failure must never print the data-loss line.
    param($L, [int]$ReadFailCode, [string]$When)
    $reads = @(
        [pscustomobject]@{ Key = 'episodic header md5';   Kind = 'md5';   Cmd = (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.EpiHeaderLba -Count 1) }
        [pscustomobject]@{ Key = 'episodic header magic'; Kind = 'magic'; Cmd = (Get-CmdSectorMagic -Lba $L.EpiHeaderLba) }
        [pscustomobject]@{ Key = 'episodic tail md5';     Kind = 'md5';   Cmd = (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.EpiTailLba -Count $L.AnchorCount) }
        [pscustomobject]@{ Key = 'JACT head md5';         Kind = 'md5';   Cmd = (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.ActHeadLba -Count $L.AnchorCount) }
        [pscustomobject]@{ Key = 'JACT head magic';       Kind = 'magic'; Cmd = (Get-CmdSectorMagic -Lba $L.ActHeadLba) }
    )
    $vals = [ordered]@{}
    foreach ($r in $reads) {
        $res = Invoke-Box -RemoteCommand $r.Cmd
        $v = if ($r.Kind -eq 'magic') { Get-HexOut $res.Out } else { $res.Out }
        $shape = if ($r.Kind -eq 'magic') { '^[0-9a-f]{8}$' } else { '^[0-9a-f]{32}$' }
        if ($res.Code -ne 0 -or $v -notmatch $shape) {
            Fail $ReadFailCode ("could not READ the anchor '{0}' {1} (exit {2}, output '{3}'). This is an ssh/sudo READ failure; the sector is NOT proven changed." -f $r.Key, $When, $res.Code, $v)
            return $null
        }
        $vals[$r.Key] = $v
        Info ('anchor {0,-22} {1}: {2}' -f $r.Key, $When, $v)
    }
    if ($vals['episodic header magic'] -ne $L.EpiMagicHex) {
        Fail 62 ("the episodic header at LBA {0} reads magic '{1}' {2}, expected '{3}' (JEPI)" -f $L.EpiHeaderLba, $vals['episodic header magic'], $When, $L.EpiMagicHex)
        return $null
    }
    if ($vals['JACT head magic'] -ne $L.ActMagicHex) {
        Fail 62 ("the JACT head at LBA {0} reads magic '{1}' {2}, expected '{3}' (JACT)" -f $L.ActHeadLba, $vals['JACT head magic'], $When, $L.ActMagicHex)
        return $null
    }
    Pass ('the three anchors read {0}: episodic header (JEPI), episodic tail, JACT head (JACT)' -f $When)
    return $vals
}

function Compare-JsemAnchors {
    param($Before, $After)
    foreach ($k in $Before.Keys) {
        if ($After[$k] -ne $Before[$k]) {
            Fail 62 ("ANCHOR CHANGED: {0} was {1} before the write and is {2} after. THIS IS A DATA-LOSS EVENT: a JSEM write reached a neighbouring store. Stop and assess before booting JARVIS; the pre-image is retained." -f $k, $Before[$k], $After[$k])
            return $false
        }
    }
    Pass 'A1 every anchor is unchanged'
    return $true
}

function Test-JsemBoxClone {
    # Gate 5. Ancestry is a claim about HEAD while python3 runs the WORKING TREE's
    # file, so the file's own status and presence are checked too. merge-base's
    # exit code is reported, never interpreted: a clone that predates the commit
    # exits 128 (the object is unknown to it), not 1.
    param([int]$Code)
    $anc = Invoke-Box -RemoteCommand ('git -C {0} merge-base --is-ancestor {1} HEAD' -f $BoxRepo, $ParserCommit)
    if ($anc.Code -ne 0) {
        Fail $Code ("the box clone {0} does not hold {1} as an ancestor of HEAD (git merge-base --is-ancestor exited {2}). Pull it forward on the box: git -C {0} pull --ff-only" -f $BoxRepo, $ParserCommit, $anc.Code)
        return $false
    }
    $st = Invoke-Box -RemoteCommand ('git -C {0} status --porcelain -- phase3/scripts/parse_semantic.py' -f $BoxRepo)
    if ($st.Code -ne 0 -or $st.Out -ne '') {
        Fail $Code ("the box clone's phase3/scripts/parse_semantic.py is not clean (exit {0}, status '{1}') -- python3 runs the working-tree file, so it must be the committed one" -f $st.Code, $st.Out)
        return $false
    }
    $tf = Invoke-Box -RemoteCommand ('test -f {0}/phase3/scripts/parse_semantic.py' -f $BoxRepo)
    if ($tf.Code -ne 0) {
        Fail $Code ('{0}/phase3/scripts/parse_semantic.py is not a file on the box' -f $BoxRepo)
        return $false
    }
    Pass ('the box clone {0} holds {1} and a clean parse_semantic.py' -f $BoxRepo, $ParserCommit)
    return $true
}

function Test-JsemLocalImage {
    # L1, L1b and L2. Returns the image's figures, or $null after a Fail. With
    # -Report (the -Check form) L1b is a Pass/Warn report, never a Fail: after a
    # later re-projection the default path holds another household's image.
    param($L, [switch]$Report)
    if (-not (Test-Path -LiteralPath $Image)) { Fail 90 ("L1 the image '{0}' does not exist -- build it with the memory store's project verb" -f $Image); return $null }
    if (-not (Test-Path -LiteralPath $Manifest)) { Fail 90 ("L1 the manifest '{0}' does not exist -- the project run that wrote the image writes it too" -f $Manifest); return $null }
    try {
        $man = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
        $n = [uint64]$man.n
        $mImage = [string]$man.md5_image
        $mHeader = [string]$man.md5_header
        $mRecords = [string]$man.md5_records
    } catch {
        Fail 90 ("L1 the manifest '{0}' could not be read as a projection manifest: {1}" -f $Manifest, $_.Exception.Message)
        return $null
    }
    $bytes = [IO.File]::ReadAllBytes($Image)
    if ([uint64]$bytes.Length -ne $L.RegionBytes) {
        Fail 90 ("L1 the image '{0}' is {1} bytes, expected exactly {2}" -f $Image, $bytes.Length, $L.RegionBytes)
        return $null
    }
    $w = Get-BytesMd5 -Bytes $bytes -Offset 0 -Count $bytes.Length
    $h = Get-BytesMd5 -Bytes $bytes -Offset 0 -Count 512
    $r = Get-BytesMd5 -Bytes $bytes -Offset 512 -Count ($bytes.Length - 512)
    if ($w -ne $mImage -or $h -ne $mHeader -or $r -ne $mRecords) {
        Fail 90 ("L1 the image's md5s (whole {0}, header {1}, records {2}) are not the manifest's ({3}, {4}, {5}) -- the image and manifest are from different runs" -f $w, $h, $r, $mImage, $mHeader, $mRecords)
        return $null
    }
    Pass ('L1 image {0} B, md5 whole {1}, header {2}, records {3} -- equal to the manifest' -f $bytes.Length, $w, $h, $r)
    if ($w -ne $ExpectImageMd5.ToLower()) {
        if ($Report) {
            Warn ('L1b the image md5 {0} is not -ExpectImageMd5 {1} -- a -Project would refuse it at gate 2b (reported here, never gated)' -f $w, $ExpectImageMd5)
        } else {
            Fail 90 ("L1b the image md5 {0} is not the expected image {1}. This is the one check that pins WHICH household is written; pass -ExpectImageMd5 only after deciding this image is the one to write." -f $w, $ExpectImageMd5)
            return $null
        }
    } else {
        Pass ('L1b the image is the expected image ({0})' -f $w)
    }
    $magic = [BitConverter]::ToUInt32($bytes, 0)
    $ver = [BitConverter]::ToUInt32($bytes, 4)
    $cur = [BitConverter]::ToUInt32($bytes, 8)
    $tot = [BitConverter]::ToUInt32($bytes, 12)
    if ([uint64]$magic -ne $L.SemMagic -or [uint64]$ver -ne $L.SemVersion -or [uint64]$tot -ne $n -or [uint64]$cur -ne ($n % $L.MaxFacts)) {
        Fail 91 ('L2 the image header decodes to magic 0x{0:X8}, version {1}, cursor {2}, total {3}; expected 0x{4:X8}, {5}, {6}, {7}' -f $magic, $ver, $cur, $tot, $L.SemMagic, $L.SemVersion, ($n % $L.MaxFacts), $n)
        return $null
    }
    Pass ('L2 the image header decodes to JSEM, version {0}, total {1} (the manifest''s n), cursor {2}' -f $ver, $tot, $cur)
    return [pscustomobject]@{ N = $n; Md5Image = $w; Md5Header = $h; Md5Records = $r }
}

function Test-JsemSemanticOff {
    # L3. Byte-equality is asserted for the JARVIS_SEMANTIC = 0 regime only: a
    # gated slice's sem_store_init rewrites the header on every boot.
    try { $v = Get-HeaderConst -Path $hdrDebug -Name 'JARVIS_SEMANTIC' }
    catch { Fail 6 $_.Exception.Message; return $false }
    if ($v -ne 0) {
        Fail 92 ('L3 JARVIS_SEMANTIC is {0} in jarvis_debug.h -- refusing: the write is pre-registered for the JARVIS_SEMANTIC = 0 regime only' -f $v)
        return $false
    }
    Pass 'L3 JARVIS_SEMANTIC is 0 -- nothing on the box reads the region'
    return $true
}

function Remove-BoxFile {
    # Removes each name from the box's home and PROVES it gone with test ! -e,
    # because rm -f exits 0 for a file that never existed.
    param([string[]]$Names)
    $ok = $true
    foreach ($nm in $Names) {
        [void](Invoke-Box -RemoteCommand (Get-CmdBoxRemove -Name $nm))
        $gone = Invoke-Box -RemoteCommand ('test ! -e $HOME/{0}' -f $nm)
        if ($gone.Code -ne 0) { Warn ('~/{0} is still on the box -- remove it by hand' -f $nm); $ok = $false }
    }
    return $ok
}

function Send-ToBox {
    # scp with a BARE file name, run from the file's own directory.
    param([string]$LocalPath, [string]$RemoteName)
    $dir = Split-Path -Parent $LocalPath
    $leaf = Split-Path -Leaf $LocalPath
    $shown = Get-CmdScpTo -BoxHost $BoxHost -Dir $dir -LocalName $leaf -RemoteName $RemoteName
    Info $shown
    Push-Location $dir
    try { & scp $leaf ('{0}:{1}' -f $BoxHost, $RemoteName) | Out-Null; $rc = $LASTEXITCODE } finally { Pop-Location }
    Write-Transcript -Command $shown -ExitCode $rc
    return $rc
}

function Receive-FromBox {
    param([string]$RemoteName, [string]$LocalDir)
    $shown = Get-CmdScpFrom -BoxHost $BoxHost -Dir $LocalDir -RemoteName $RemoteName -LocalName $RemoteName
    Info $shown
    Push-Location $LocalDir
    try { & scp ('{0}:{1}' -f $BoxHost, $RemoteName) $RemoteName | Out-Null; $rc = $LASTEXITCODE } finally { Pop-Location }
    Write-Transcript -Command $shown -ExitCode $rc
    return $rc
}

function Show-JsemWrittenBanner {
    # Printed by Fail's post-write hook and by a JSEM block's finally, once a run.
    $t = Get-JsemWrittenBanner -Mode $script:jsemMode -LocalPre $script:jsemLocalPre -BoxHost $BoxHost -Leaf $script:jsemLeaf
    Write-Host ''
    Write-Host ('  ' + ('!' * 78)) -ForegroundColor Red
    Write-Host ('  {0}' -f $t) -ForegroundColor Red
    Write-Host ('  ' + ('!' * 78)) -ForegroundColor Red
    $script:jsemBannerShown = $true
}

function Test-JsemStaged {
    # DW-2: a staged source re-checked AFTER the typed word, which can wait for as
    # long as the operator likes. The commands are the ones the pre-prompt check
    # ran (-Slice: Get-CmdFileSliceMd5 whole, the restore's; otherwise
    # Get-CmdBoxFileMd5, T2's), so the bytes written are the bytes verified.
    param([string]$Name, [uint64]$Bytes, [string]$Md5, [int]$Code, [string]$Label, [string]$After, [switch]$Slice)
    $sz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $Name)
    $mdCmd = if ($Slice) { Get-CmdFileSliceMd5 -Name $Name -Part 'whole' } else { Get-CmdBoxFileMd5 -Name $Name }
    $md = Invoke-Box -RemoteCommand $mdCmd
    if ($sz.Code -ne 0 -or $sz.Out -ne [string]$Bytes -or $md.Code -ne 0 -or $md.Out -ne $Md5) {
        Fail $Code ("{0} changed after {1}: ~/{2} is now '{3}' bytes, md5 '{4}' (exits {5}, {6}); it was {7} bytes, md5 {8} -- nothing was written" -f $Label, $After, $Name, $sz.Out, $md.Out, $sz.Code, $md.Code, $Bytes, $Md5)
        return $false
    }
    Pass ('~/{0} re-verified after the typed word: {1} B, md5 {2} -- unchanged since {3}' -f $Name, $sz.Out, $md.Out, $After)
    return $true
}

# ---------------------------------------------------------------------------
# -Check's projection section. Gated on its OWN probe, never on the key
# preflight: Invoke-Preflight returns $null on a key-side failure, and a moved or
# mismatched control_key.bin must not silently skip every JSEM leg. It really
# executes every shape without writing the device.
# ---------------------------------------------------------------------------
function Invoke-CheckProjection {
    Say ''
    Say '== JSEM projection (-Project / -ProjectRestore): every shape run for real, the device never written =='
    Info 'gated on its OWN probe, not on the key preflight: P2-P4 are key gates, deliberately not consulted here, and P5 (the receiver) does not apply to a JSEM write'
    if (-not (Test-JsemTools)) { Info 'projection section skipped: a required tool is missing'; return }
    $L = Get-JsemLayoutChecked
    if (-not $L) { Info 'projection section skipped: the layout could not be derived'; return }
    $probe = Invoke-Box -RemoteCommand 'true'
    if ($probe.Code -ne 0) {
        Info 'projection section SKIPPED: its own P1 probe did not reach the box over ssh'
        Fail 10 ("ssh to '{0}' failed -- the projection section did not run, so this -Check cannot pass" -f $BoxHost)
        return
    }
    Pass ("P1 box reachable over ssh ('{0}' answered)" -f $BoxHost)

    $probeImg = 'jsem_probe.img'
    $probeOut = 'jsem_probe.out'
    $probeB = 'jsem_probe_b.img'
    $probeRead = 'jsem_probe_read.bin'
    $probeParse = 'jsem_probe_img.bin'
    $sd = Join-Path $env:TEMP ('jarvis-admin-jsem-{0}' -f (Get-Random))
    New-Item -ItemType Directory -Path $sd -Force | Out-Null
    try {
        # -- 1. the two-part write, rehearsed on a FILE --------------------------
        Say ''
        Say '  -- 1. the two-part write, rehearsed on a FILE on the box (never the device) --'
        if (Remove-BoxFile -Names @($probeOut, $probeImg)) {
            Pass 'no stale probe: ~/jsem_probe.out and ~/jsem_probe.img proven absent before the rehearsal'
        } else {
            Fail 5 'a stale rehearsal probe could not be removed -- with notrunc a stale, longer target survives the writes and fails the leg for the wrong reason'
        }
        $rng = New-Object System.Random 20260926
        $buf = New-Object byte[] ([int]$L.RegionBytes)
        $rng.NextBytes($buf)
        $localA = Join-Path $sd $probeImg
        [IO.File]::WriteAllBytes($localA, $buf)
        $mdA = Get-FileMd5 -Path $localA
        $bufB = [byte[]]$buf.Clone()
        $bufB[100] = [byte]($bufB[100] -bxor 0xFF)
        $bufB[70000] = [byte]($bufB[70000] -bxor 0xFF)
        $localB = Join-Path $sd $probeB
        [IO.File]::WriteAllBytes($localB, $bufB)
        Info ('throwaway: {0} bytes from a seeded pattern, md5 {1}' -f $buf.Length, $mdA)
        $upA = Send-ToBox -LocalPath $localA -RemoteName $probeImg
        if ($upA -ne 0) {
            Fail 5 ('the scp shape FAILED (exit {0}) -- host:path parsing or auth' -f $upA)
        } else {
            $rplan = @(Get-JsemWritePlan -Device '$HOME/jsem_probe.out' -Name $probeImg -BaseLba 0 -MaxFacts $L.MaxFacts)
            $wok = $true
            foreach ($cmd in $rplan) {
                Info $cmd
                $wr = Invoke-Box -RemoteCommand $cmd
                if ($wr.Code -ne 0) { Fail 5 ('the rehearsal write FAILED (exit {0}): {1}' -f $wr.Code, $cmd); $wok = $false; break }
            }
            if ($wok) {
                $sz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $probeOut)
                $m5 = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $probeOut)
                if ($sz.Code -ne 0 -or $sz.Out -ne [string]$L.RegionBytes -or $m5.Out -ne $mdA) {
                    Fail 5 ("the rehearsal target is '{0}' bytes, md5 '{1}'; the throwaway is {2} bytes, md5 {3}. A 512-byte target means the header write truncated the file: notrunc is missing." -f $sz.Out, $m5.Out, $L.RegionBytes, $mdA)
                } else {
                    Pass ('rehearsal: records then header into a FILE, which ends {0} bytes, md5 {1} -- identical to the throwaway' -f $sz.Out, $m5.Out)
                }
                # The two md5 slices -Project's R2 and the restore's readback run,
                # against md5s computed HERE from the throwaway's own bytes.
                $sh = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $probeOut -Part 'header')
                $sr = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $probeOut -Part 'records')
                $wantH = Get-BytesMd5 -Bytes $buf -Offset 0 -Count 512
                $wantR = Get-BytesMd5 -Bytes $buf -Offset 512 -Count ($buf.Length - 512)
                if ($sh.Code -ne 0 -or $sh.Out -ne $wantH -or $sr.Code -ne 0 -or $sr.Out -ne $wantR) {
                    Fail 5 ("the md5 slice shapes FAILED: header '{0}' (exit {1}), records '{2}' (exit {3}); the throwaway's are {4}, {5}" -f $sh.Out, $sh.Code, $sr.Out, $sr.Code, $wantH, $wantR)
                } else {
                    Pass ('md5 slice shapes work: header {0}, records {1} -- equal to the throwaway''s, computed on this PC' -f $sh.Out, $sr.Out)
                }
                # The pull of a file back to this PC (gate 9's shape).
                $rcPull = Receive-FromBox -RemoteName $probeOut -LocalDir $sd
                $pulled = Join-Path $sd $probeOut
                $pmd = if (Test-Path -LiteralPath $pulled) { Get-FileMd5 -Path $pulled } else { '' }
                if ($rcPull -ne 0 -or $pmd -ne $mdA) {
                    Fail 5 ("the pull shape FAILED (scp exit {0}); the pulled copy's md5 is '{1}', the throwaway's {2}" -f $rcPull, $pmd, $mdA)
                } else {
                    Pass ('pull shape works: ~/{0} pulled to this PC, md5 {1} -- identical to the throwaway' -f $probeOut, $pmd)
                }
            }
        }
        Info 'the rehearsal covers: the builder''s real conv (fsync,notrunc), the input skip, the count split, the plan''s base + 1 arithmetic and the records-then-header order'
        Info 'it does NOT cover the ABSOLUTE device seeks (a device seek against a file would address 10.8 GB in): the unit test asserts the plan at the real base and item 7 below prints it; on the operator''s run the readback (R2) proves the region''s final bytes, the anchors (A1) prove that three sampled neighbours did not move, and the transcript records the exact command strings that ran'

        # -- 2. the read shapes, against the real device --------------------------
        Say ''
        Say '  -- 2. the read shapes, against the real device --'
        $reg = Invoke-Box -RemoteCommand (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount)
        $regMd5 = ''
        if ($reg.Code -ne 0 -or $reg.Out -notmatch '^[0-9a-f]{32}$') {
            Fail 5 ("the range-md5 shape FAILED over the region (exit {0}, output '{1}')" -f $reg.Code, $reg.Out)
        } else {
            $regMd5 = $reg.Out
            $manMd5 = ''
            if (Test-Path -LiteralPath $Manifest) {
                try { $manMd5 = [string](Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json).md5_image } catch { $manMd5 = '' }
            }
            Info ('region md5 now      : {0}' -f $regMd5)
            Info ('-ExpectPreMd5       : {0}' -f $ExpectPreMd5)
            if ($manMd5) { Info ('manifest md5_image  : {0}' -f $manMd5) }
            if ($regMd5 -eq $ExpectPreMd5.ToLower()) { Pass 'the region holds the expected pre-image (-ExpectPreMd5) -- a report, not a gate' }
            elseif ($manMd5 -and $regMd5 -eq $manMd5) { Pass 'the region holds the manifest''s image -- the projection is already written (a report, not a gate)' }
            else { Warn 'the region matches neither the expected pre-image nor the manifest''s image -- -Project would refuse at gate 8 (reported here, never gated)' }
        }
        [void](Read-JsemAnchors -L $L -ReadFailCode 5 -When 'now')
        $tf = Invoke-Box -RemoteCommand (Get-CmdRangeToFile -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount -Name $probeRead)
        $tsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $probeRead)
        $tmd = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $probeRead)
        if ($tf.Code -ne 0 -or $tsz.Code -ne 0 -or $tsz.Out -ne [string]$L.RegionBytes) {
            Fail 5 ("the range-to-file shape FAILED (exit {0}; the file is '{1}' bytes, expected {2})" -f $tf.Code, $tsz.Out, $L.RegionBytes)
        } elseif ($regMd5 -and $tmd.Out -ne $regMd5) {
            Fail 5 ("the two read shapes disagree: the file copy's md5 is '{0}', the piped md5 was {1}" -f $tmd.Out, $regMd5)
        } else {
            Pass ('range-to-file shape works: {0} bytes, md5 {1} -- agrees with the piped md5' -f $tsz.Out, $tmd.Out)
        }

        # -- 3. the cache and diagnostic shapes -----------------------------------
        Say ''
        Say '  -- 3. the cache and diagnostic shapes --'
        $dc = Invoke-Box -RemoteCommand (Get-CmdDropCaches)
        if ($dc.Code -ne 0) { Fail 95 ('the drop_caches shape FAILED (exit {0}): {1}' -f $dc.Code, (Get-CmdDropCaches)) }
        else { Pass ('drop_caches shape works: {0}' -f (Get-CmdDropCaches)) }
        $upB = if ($upA -eq 0) { Send-ToBox -LocalPath $localB -RemoteName $probeB } else { 1 }
        if ($upB -ne 0) {
            Fail 5 'the cmp probe could not be staged (see the scp result above)'
        } else {
            $cm = Invoke-Box -RemoteCommand (Get-CmdCmpHead -A $probeImg -B $probeB)
            $lines = @($cm.Out -split "`n" | Where-Object { $_.Trim() -ne '' })
            $got = @($lines | ForEach-Object {
                $t = @($_.Trim() -split '\s+')
                if ($t.Count -eq 3) { '{0}:{1}:{2}' -f $t[0], [Convert]::ToInt32($t[1], 8), [Convert]::ToInt32($t[2], 8) } else { $_ }
            })
            $want = @(('101:{0}:{1}' -f $buf[100], $bufB[100]), ('70001:{0}:{1}' -f $buf[70000], $bufB[70000]))
            if (($got -join ',') -ne ($want -join ',')) {
                Fail 5 ("the cmp shape did not name the two known bytes: got '{0}', expected '{1}'" -f ($got -join ', '), ($want -join ', '))
            } else {
                Pass ('cmp shape names both known bytes (offset:old:new, decimal) {0}' -f ($got -join ', '))
            }
        }

        # -- 4. the box clone ----------------------------------------------------
        Say ''
        Say '  -- 4. the box clone (gate 5) --'
        [void](Test-JsemBoxClone -Code 7)

        # -- 5. the local gates --------------------------------------------------
        Say ''
        Say '  -- 5. the local gates (L1, L1b reported, L2, L3) --'
        $limg = $null
        if ((Test-Path -LiteralPath $Image) -and (Test-Path -LiteralPath $Manifest)) {
            $limg = Test-JsemLocalImage -L $L -Report
        } else {
            Info ("L1/L1b/L2 skipped: no image at '{0}' or no manifest at '{1}' yet" -f $Image, $Manifest)
        }
        [void](Test-JsemSemanticOff)

        # -- 5b. the parse (gate 18's shape) ---------------------------------------
        Say ''
        Say '  -- 5b. parse_semantic.py on the box (gate 18) --'
        if ($null -ne $limg) {
            $upI = Send-ToBox -LocalPath $Image -RemoteName $probeParse
            if ($upI -ne 0) {
                Fail 5 ('the parse probe could not be staged (scp exit {0})' -f $upI)
            } else {
                $ps = Invoke-Box -RemoteCommand (Get-CmdParseSemantic -BoxRepo $BoxRepo -Name $probeParse)
                $pc = -1
                if ($ps.Code -eq 0) { try { $pc = @(($ps.Out | ConvertFrom-Json).records).Count } catch { $pc = -1 } }
                if ($ps.Code -ne 0 -or $pc -ne [int]$limg.N) {
                    Fail 5 ('the parse shape read {0} records (exit {1}) off the verified image; the manifest''s n is {2}' -f $pc, $ps.Code, $limg.N)
                } else {
                    Pass ('parse shape works: parse_semantic.py reads {0} records off ~/{1} (the verified image) -- the manifest''s n' -f $pc, $probeParse)
                }
            }
        } else {
            # No valid local image: a shape check only, over the region's own copy.
            $ps = Invoke-Box -RemoteCommand (Get-CmdParseSemantic -BoxRepo $BoxRepo -Name $probeRead)
            $pobj = $null
            if ($ps.Code -eq 0) { try { $pobj = $ps.Out | ConvertFrom-Json } catch { $pobj = $null } }
            if ($ps.Code -ne 0 -or $null -eq $pobj -or @($pobj.PSObject.Properties.Name) -notcontains 'records') {
                Fail 5 ('the parse shape FAILED over ~/{0} (exit {1}, no records array)' -f $probeRead, $ps.Code)
            } else {
                Pass ('parse shape works (no valid local image, so over the region''s copy ~/{0}, a shape check only): {1} records' -f $probeRead, @($pobj.records).Count)
            }
        }
    } finally {
        # -- 6. cleanup ------------------------------------------------------------
        Say ''
        Say '  -- 6. cleanup: every probe removed and proven absent --'
        if (Remove-BoxFile -Names @($probeImg, $probeOut, $probeB, $probeRead, $probeParse)) {
            Pass 'every box probe (jsem_probe.img, jsem_probe.out, jsem_probe_b.img, jsem_probe_read.bin, jsem_probe_img.bin) proven absent'
        } else {
            Fail 5 'a box probe could not be removed (named above)'
        }
        Remove-Item -LiteralPath $sd -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $sd) { Fail 5 ("the local probe directory '{0}' could not be removed" -f $sd) }
        else { Pass 'the local probe directory is gone' }
    }

    # -- 7. the device plan, printed and never run -------------------------------
    Say ''
    Say '  -- 7. the device plan -Project would run (printed here, NEVER run) --'
    $dplan = @(Get-JsemWritePlan -Device $BoxDevice -Name 'jsem.img' -BaseLba $L.BaseLba -MaxFacts $L.MaxFacts)
    Say ('     records: {0}' -f $dplan[0])
    Say ('     header : {0}' -f $dplan[1])
    Info 'the ONLY shapes NOT exercised are these two device writes, which cannot be without writing the device; every other command shape -Project and -ProjectRestore send to the box, and the push and the pull, was executed above'
}

# ---------------------------------------------------------------------------
# The chooser (bare invocation only). It owns SELECTION, never logic: each
# option RE-INVOKES this same script with explicit flags, so the chosen mode
# runs the identical code path the CLI runs and cannot drift from it. That is
# jarvis_menu.ps1's R1 rule applied to ourselves.
# ---------------------------------------------------------------------------
function Get-MenuStatus {
    # Read-only probe. Deliberately does NOT call Fail: the menu must render in
    # every state, including the broken ones it exists to warn about.
    $st = [pscustomobject]@{ BoxUp = $false; FpBox = ''; FpPc = ''; State = 'UNREACHABLE'; Detail = '' }
    if (Test-Path -LiteralPath $KeyFile) {
        try {
            $kb = [IO.File]::ReadAllBytes($KeyFile)
            if ($kb.Length -eq [int]$KEY_LEN) { $st.FpPc = Get-FpFromBytes $kb }
            else { $st.FpPc = ('BAD({0}B)' -f $kb.Length) }
        } catch { $st.FpPc = 'UNREADABLE' }
    } else { $st.FpPc = 'MISSING' }

    $probe = Invoke-Box -RemoteCommand 'true'
    if ($probe.Code -ne 0) {
        $st.Detail = 'ssh did not answer -- the box is most likely booted into JARVIS (seL4 has no sshd), or off'
        return $st
    }
    $st.BoxUp = $true
    $fp = Invoke-Box -RemoteCommand (Get-CmdSectorFp -Lba $KEY_LBA)
    $st.FpBox = if ($fp.Code -eq 0 -and $fp.Out.Length -eq 8) { $fp.Out } else { 'UNREADABLE' }

    if ($st.FpBox -eq 'UNREADABLE' -or $st.FpPc -notmatch '^[0-9a-f]{8}$') {
        $st.State = 'DIFFER'; $st.Detail = 'one half could not be read'
    } elseif ($st.FpBox -eq $st.FpPc) {
        $st.State = 'MATCH'
    } else {
        $st.State  = 'DIFFER'
        $st.Detail = 'the channel is ALREADY broken -- re-keying is the wrong action here; use Rollback'
    }
    return $st
}

function Invoke-Menu {
    $psExe = 'powershell.exe'
    try { $pp = (Get-Process -Id $PID).Path; if ($pp) { $psExe = $pp } } catch {}
    # Forwarded so a child runs against exactly the values this invocation was
    # given, not the defaults. Built by the PURE Get-ForwardArgs (unit-tested,
    # because the chooser cannot be driven from a redirected stdin), which drops
    # empty values: -PreImage is '' unless given, and an empty native argument
    # would reach the child as a bare -PreImage and abort it.
    $fwd = [ordered]@{
        KeyFile        = $KeyFile
        BoxHost        = $BoxHost
        Port           = "$Port"
        Image          = $Image
        Manifest       = $Manifest
        ExpectPreMd5   = $ExpectPreMd5
        ExpectImageMd5 = $ExpectImageMd5
        PreImage       = $PreImage
        BoxRepo        = $BoxRepo
    }
    $common = @(Get-ForwardArgs -Params $fwd)

    $choices = @(
        [pscustomobject]@{ Key='1'; Label='Check';                Note='full validation + dry run; writes nothing to the device'; CArgs=@('-Check');                NeedsSsh=$false; Terminal=$false }
        [pscustomobject]@{ Key='2'; Label='Re-key  KEEP backups'; Note='retains the old key (needed to prove revocation)';        CArgs=@('-Rekey','-KeepBackups'); NeedsSsh=$true;  Terminal=$true  }
        [pscustomobject]@{ Key='3'; Label='Re-key';               Note='deletes backups after success (default behaviour)';       CArgs=@('-Rekey');                NeedsSsh=$true;  Terminal=$true  }
        [pscustomobject]@{ Key='4'; Label='Rollback';             Note='restore both halves from the .BAK pair';                  CArgs=@('-Rollback');             NeedsSsh=$true;  Terminal=$true  }
        [pscustomobject]@{ Key='5'; Label='Project (dry run)';    Note='JSEM gates 1-10 and the plan; writes nothing';           CArgs=@('-Project','-DryRun');    NeedsSsh=$true;  Terminal=$false }
        [pscustomobject]@{ Key='6'; Label='Project';              Note='write the JSEM image (PROJECT-WRITE-NOW)';                CArgs=@('-Project');              NeedsSsh=$true;  Terminal=$true  }
        [pscustomobject]@{ Key='7'; Label='ProjectRestore';       Note='write the retained pre-image back (PROJECT-RESTORE-NOW)'; CArgs=@('-ProjectRestore');       NeedsSsh=$true;  Terminal=$true  }
    )

    Say ''
    Say '  probing the box...'
    $st = Get-MenuStatus
    $empties = 0

    while ($true) {
        $stateColor = switch ($st.State) { 'MATCH' { 'Green' } 'DIFFER' { 'Red' } default { 'Yellow' } }
        Say ''
        Say '====== jarvis_admin -- control-IN re-key and JSEM projection (DESTRUCTIVE) ======'
        Write-Host -NoNewline '  box: '
        if ($st.BoxUp) { Write-Host 'UBUNTU (ssh up)' -ForegroundColor Green -NoNewline }
        else { Write-Host 'UNREACHABLE' -ForegroundColor Yellow -NoNewline }
        Write-Host -NoNewline '      halves: '
        if ($st.BoxUp) {
            Write-Host ('{0} (box {1} / pc {2})' -f $st.State, $st.FpBox, $st.FpPc) -ForegroundColor $stateColor
        } else {
            Write-Host ('UNKNOWN (pc {0})' -f $st.FpPc) -ForegroundColor Yellow
        }
        if ($st.Detail) { Write-Host ('  {0}' -f $st.Detail) -ForegroundColor $stateColor }
        Say ''
        foreach ($c in $choices) {
            $line = '   {0}  {1,-22} {2}' -f $c.Key, $c.Label, $c.Note
            # Shown DISABLED with the reason, never hidden: a hidden option reads
            # as a missing feature (jarvis_menu.ps1's R3).
            if ($c.NeedsSsh -and -not $st.BoxUp) { Write-Host ($line + '   [unavailable]') -ForegroundColor DarkGray }
            else { Write-Host $line }
        }
        if (-not $st.BoxUp) {
            Say ''
            Write-Host '   ^ unavailable: these need ssh, and the box is not answering.' -ForegroundColor DarkGray
            Write-Host '     If it is booted into JARVIS that is exactly the state in which a re-key or a' -ForegroundColor DarkGray
            Write-Host '     JSEM write must NOT proceed -- boot it into Ubuntu first. Check still runs and reports.' -ForegroundColor DarkGray
        }
        Say ''
        Say '   r  Refresh status'
        Say '   q  Quit'
        Say '=================================================================================='

        $pick = Read-Host 'choose'
        $pick = "$pick".Trim().ToLower()

        if ($pick -eq '') {
            # IsInputRedirected is FALSE for a console driven to EOF (Ctrl+Z), so
            # the guard at the top does not cover it and this does.
            $empties++
            if ($empties -ge 3) { Say '  empty input three times running (stdin at EOF?) -- exiting rather than spinning.'; return 0 }
            continue
        }
        $empties = 0

        if ($pick -eq 'q' -or $pick -eq 'quit') { Write-Transcript -Command 'menu:q -> quit (nothing was run)' -ExitCode 0; return 0 }
        if ($pick -eq 'r') { Say ''; Say '  probing the box...'; $st = Get-MenuStatus; continue }

        $c = $choices | Where-Object { $_.Key -eq $pick } | Select-Object -First 1
        if (-not $c) { Write-Host ('  not an option: "{0}"' -f $pick) -ForegroundColor Yellow; continue }
        if ($c.NeedsSsh -and -not $st.BoxUp) {
            Write-Host '  unavailable while the box is unreachable -- boot it into Ubuntu, then press r.' -ForegroundColor Yellow
            continue
        }

        $argList = @('-NoProfile','-ExecutionPolicy','Bypass','-File', $PSCommandPath) + $c.CArgs + $common
        $resolved = ($c.CArgs -join ' ')
        # The FORWARDED values are recorded beside the selection: the transcript is
        # the audit trail for a device write, and "menu:6 -> -Project" alone does
        # not say which image was written or which pre-image was expected.
        $forwarded = ($common -join ' ')
        # Record the SELECTION and the RESOLVED action: "which option did I
        # actually pick" is the first question when something goes wrong.
        Write-Transcript -Command ('menu:{0} -> {1} | forwarded: {2}' -f $c.Key, $resolved, $forwarded) -ExitCode 'started'
        Say ''
        Say ('  running  : jarvis_admin.ps1 {0}' -f $resolved)
        Say ('  forwarded: {0}' -f $forwarded)
        Say ''
        & $psExe @argList
        $rc = $LASTEXITCODE
        Write-Transcript -Command ('menu:{0} -> {1} | forwarded: {2}' -f $c.Key, $resolved, $forwarded) -ExitCode $rc

        if ($c.Terminal) {
            # Re-keys and rollbacks are terminal: returning to a menu after one
            # invites a second run against state that just changed underneath it.
            Say ''
            Say ('  {0} finished with exit code {1}. This menu does not loop after a write.' -f $c.Label, $rc)
            return $rc
        }
        Say ''
        Say ('  {0} finished (exit {1}).' -f $c.Label, $rc)
        $st = Get-MenuStatus
    }
}

if ($MenuMode) { exit (Invoke-Menu) }

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

    # The JSEM projection section: its own probe, its own gates, the same one
    # exit code and verdict line (Fail keeps the worst code across both halves).
    Invoke-CheckProjection

    Say ''
    if ($script:worstExit -eq 0) {
        Say '== -Check PASS -- a -Rekey could proceed right now, and every -Project shape works =='
    } else {
        Say ("== -Check FAIL (exit {0}) -- fix the FAIL lines above before -Rekey or -Project ==" -f $script:worstExit)
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
# -Project -- the JSEM projection write (Phase 7 MS3b), gates 1-19.
#
# PLACEMENT IS LOAD-BEARING. Everything after the -Rekey header below is the
# re-key procedure, unguarded. This block and -ProjectRestore's sit ABOVE it and
# every terminating path of each ends in `exit` -- Fail exits in these modes, and
# each block's last statement is an exit. A block without one would fall through
# into the re-key backups, key generation and REKEY-WRITE-NOW. No runtime check
# can see that, so test_jarvis_admin_jsem.ps1 asserts it on the AST.
# ===========================================================================
if ($Project) {
    $dryTag = if ($DryRun) { ' -DryRun (gates 1-10 for real, then the plan; nothing is written)' } else { '' }
    Say ('== jarvis_admin.ps1 -Project{0} ==' -f $dryTag)

    Say ''
    Say '== gate 1: tools, constants, P1 =='
    [void](Test-JsemTools)
    $L = Get-JsemLayoutChecked
    [void](Test-JsemBoxReachable)

    Say ''
    Say '== gates 2-4: the local image (L1, L1b, L2) and JARVIS_SEMANTIC (L3) =='
    $img = Test-JsemLocalImage -L $L
    [void](Test-JsemSemanticOff)

    Say ''
    Say '== gate 5: the box clone holds the parser =='
    [void](Test-JsemBoxClone -Code 7)

    Say ''
    Say '== gate 6: drop_caches works, proven BEFORE any write =='
    $dc = Invoke-Box -RemoteCommand (Get-CmdDropCaches)
    if ($dc.Code -ne 0) { Fail 95 ('drop_caches FAILED (exit {0}): {1} -- nothing was written' -f $dc.Code, (Get-CmdDropCaches)) }
    Pass ('drop_caches ran: {0}' -f (Get-CmdDropCaches))

    Say ''
    Say '== gate 7: the anchors BEFORE the write (A0) =='
    Say '  (an anchor already wrong HERE is pre-existing -- this run has not written anything yet)'
    $a0 = Read-JsemAnchors -L $L -ReadFailCode 62 -When 'before'
    # A0 persisted: the transcript records no screen output, so without this line
    # it could not show what the anchors read before the write.
    if ($null -ne $a0) {
        Write-Transcript -Command ('A0 ' + ((@($a0.Keys) | ForEach-Object { '{0}={1}' -f $_, $a0[$_] }) -join '; ')) -ExitCode 'recorded'
    }

    Say ''
    Say '== gate 8: the region must hold the expected pre-image =='
    $pre = Invoke-Box -RemoteCommand (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount)
    Info ('region md5 now : {0}' -f $pre.Out)
    Info ('-ExpectPreMd5  : {0}' -f $ExpectPreMd5)
    if ($pre.Code -ne 0 -or $pre.Out -notmatch '^[0-9a-f]{32}$') {
        Fail 94 ("the region could not be read (exit {0}, output '{1}') -- nothing was written" -f $pre.Code, $pre.Out)
    }
    if ($pre.Out -ne $ExpectPreMd5.ToLower()) {
        Fail 94 ("the region's md5 {0} is not the expected pre-image {1}. Either the region holds something unexpected OR the read was short -- this shape pipes dd into md5sum, so a short read hashes as if whole, and gate 9's size-checked read would tell the two apart. Either way nothing was written, and it is the operator's decision: an unexpected region is never overwritten." -f $pre.Out, $ExpectPreMd5)
    }
    Pass ('the region holds the expected pre-image ({0})' -f $pre.Out)

    Say ''
    Say '== gate 9: the pre-image, on the box and on this PC (B1) =='
    # UtcNow, not Get-Date -Format: that returns LOCAL time under the Z.
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
    $preName = 'jsem_pre_{0}.bin' -f $stamp
    $b1 = Invoke-Box -RemoteCommand (Get-CmdRangeToFile -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount -Name $preName)
    if ($b1.Code -ne 0) { Fail 93 ('the pre-image read FAILED (exit {0}) -- nothing was written' -f $b1.Code) }
    Add-Artifact -Path ('{0}:~/{1}' -f $BoxHost, $preName) -What 'pre-image of the JSEM region, on the box' -Region
    $bsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $preName)
    if ($bsz.Code -ne 0 -or $bsz.Out -ne [string]$L.RegionBytes) {
        Fail 93 ("the pre-image on the box is '{0}' bytes, expected {1} -- a short read" -f $bsz.Out, $L.RegionBytes)
    }
    $bmd = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name $preName)
    if ($bmd.Code -ne 0 -or $bmd.Out -ne $pre.Out) {
        Fail 93 ("the pre-image file's md5 '{0}' is not the region's {1}" -f $bmd.Out, $pre.Out)
    }
    if (-not (Test-Path -LiteralPath $adminHome)) { New-Item -ItemType Directory -Path $adminHome -Force | Out-Null }
    $localPre = Join-Path $adminHome $preName
    $rcPull = Receive-FromBox -RemoteName $preName -LocalDir $adminHome
    if ($rcPull -ne 0 -or -not (Test-Path -LiteralPath $localPre)) { Fail 93 ('pulling the pre-image back to this PC FAILED (exit {0})' -f $rcPull) }
    Add-Artifact -Path $localPre -What 'pre-image of the JSEM region, on this PC' -Region
    $lsz = (Get-Item -LiteralPath $localPre).Length
    $lmd = Get-FileMd5 -Path $localPre
    if ([uint64]$lsz -ne $L.RegionBytes -or $lmd -ne $pre.Out) {
        Fail 93 ('the pre-image on this PC is {0} bytes, md5 {1}; the box copy is {2} bytes, md5 {3}' -f $lsz, $lmd, $bsz.Out, $bmd.Out)
    }
    Pass ('B1 pre-image {0}: {1} B, md5 {2}, identical on the box and on this PC' -f $preName, $lsz, $lmd)
    # The A0 sidecar, beside the LOCAL pre-image: -ProjectRestore prints it beside
    # the anchors it reads, so a restore run later can see whether a neighbour moved.
    $sidePath = $localPre + '.anchors.txt'
    $sideVals = [ordered]@{}
    foreach ($k in $a0.Keys) { $sideVals[$k] = $a0[$k] }
    $sideVals['stamp'] = $stamp
    $sideVals['region_md5'] = $lmd
    try {
        $sideText = (@(ConvertTo-JsemAnchorLines -Values $sideVals) -join "`r`n") + "`r`n"
        [IO.File]::WriteAllText($sidePath, $sideText, [Text.Encoding]::ASCII)
        $sideBack = ConvertFrom-JsemAnchorLines -Lines ([IO.File]::ReadAllLines($sidePath))
    } catch {
        Fail 93 ("the anchors sidecar '{0}' could not be written: {1} -- nothing was written to the device" -f $sidePath, $_.Exception.Message)
    }
    Add-Artifact -Path $sidePath -What 'the anchors before this run (A0) and the pre-image md5, beside the pre-image on this PC' -Region
    if (@($sideBack.Keys).Count -ne @($sideVals.Keys).Count -or @($sideVals.Keys | Where-Object { $sideBack[$_] -ne $sideVals[$_] }).Count -ne 0) {
        Fail 93 ("the anchors sidecar '{0}' does not read back as written" -f $sidePath)
    }
    Pass ('A0 sidecar {0}: the five anchors, stamp {1}, region md5 {2}' -f (Split-Path -Leaf $sidePath), $stamp, $lmd)

    Say ''
    Say '== gate 10: the image to the box (T1/T2) =='
    $rcPush = Send-ToBox -LocalPath $Image -RemoteName 'jsem.img'
    if ($rcPush -ne 0) { Fail 40 ('scp exited {0} -- nothing was written, and the pre-image is retained' -f $rcPush) }
    Add-Artifact -Path ('{0}:~/jsem.img' -f $BoxHost) -What 'the staged image, on the box' -Staged -Name 'jsem.img'
    $tsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name 'jsem.img')
    $tmd = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name 'jsem.img')
    if ($tsz.Code -ne 0 -or $tsz.Out -ne [string]$L.RegionBytes -or $tmd.Code -ne 0 -or $tmd.Out -ne $img.Md5Image) {
        Fail 41 ("T2 the image on the box is '{0}' bytes, md5 '{1}'; the local image is {2} bytes, md5 {3} -- not byte-identical across the wire" -f $tsz.Out, $tmd.Out, $L.RegionBytes, $img.Md5Image)
    }
    Pass ('T2 ~/jsem.img on the box: {0} B, md5 {1} -- identical to the local image' -f $tsz.Out, $tmd.Out)

    # -- gate 11: the plan -------------------------------------------------------
    $plan = @(Get-JsemWritePlan -Device $BoxDevice -Name 'jsem.img' -BaseLba $L.BaseLba -MaxFacts $L.MaxFacts)
    Say ''
    Say '============================== THE JSEM WRITE =============================='
    Say ('  device       : {0}' -f $BoxDevice)
    Say ('  region       : LBA {0} for {1} sectors ({2} bytes), from semantic_store.h' -f $L.BaseLba, $L.RegionCount, $L.RegionBytes)
    Say ('  records      : LBA {0} for {1} sectors, from image sector 1 -- written FIRST' -f $L.RecordsLba, $L.MaxFacts)
    Say ('  header       : LBA {0} for 1 sector, from image sector 0 -- written LAST' -f $L.BaseLba)
    Say ('  image        : {0}  (n = {1})' -f $Image, $img.N)
    Say ('  image md5    : whole {0}, header {1}, records {2}' -f $img.Md5Image, $img.Md5Header, $img.Md5Records)
    Say ('  pre-image    : {0} and {1}:~/{2}, md5 {3}' -f $localPre, $BoxHost, $preName, $lmd)
    foreach ($k in $a0.Keys) { Say ('  anchor       : {0,-22} {1}' -f $k, $a0[$k]) }
    Say ('  write 1 of 2 : {0}' -f $plan[0])
    Say ('  write 2 of 2 : {0}' -f $plan[1])
    Say '============================================================================='
    if ($DryRun) {
        Say ''
        Say '== DRY RUN: removing what was staged, and proving it gone =='
        $okBox = Remove-BoxFile -Names @('jsem.img', $preName)
        Remove-Item -LiteralPath $localPre, $sidePath -Force -ErrorAction SilentlyContinue
        $okPc = -not (Test-Path -LiteralPath $localPre) -and -not (Test-Path -LiteralPath $sidePath)
        if (-not $okBox -or -not $okPc) {
            Fail 5 ('the dry run could not remove everything it staged -- remove by hand: ~/jsem.img and ~/{0} on the box, {1} and {2} here' -f $preName, $localPre, $sidePath)
        }
        # A stale pre-image must never become -ProjectRestore's default: the
        # operator's real run makes its own.
        $script:artifacts = @()
        Pass ('~/jsem.img and ~/{0} are gone from the box (test ! -e), and {1} and its sidecar from this PC' -f $preName, $localPre)
        Say ''
        Say 'DRY RUN - nothing was written'
        Write-Transcript -Command 'project -DryRun: gates 1-10 passed, plan printed, staged files removed, nothing written' -ExitCode 0
        exit 0
    }
    $answer = Read-Host ('  type {0} to write, anything else to abort' -f $ProjectWord)
    if (-not ("$answer".Trim() -ceq $ProjectWord)) {
        Say '  not confirmed -- NOTHING was written. The pre-image is retained on both hosts.'
        Say '  the image is still on the box at ~/jsem.img; remove it with:'
        Say ('    ssh {0} ''{1}''' -f $BoxHost, (Get-CmdBoxRemove -Name 'jsem.img'))
        Write-Transcript -Command 'project write NOT confirmed' -ExitCode 50
        Show-Artifacts
        exit 50
    }

    Say ''
    Say '== the staged image, re-verified AFTER the typed word (the prompt can wait indefinitely) =='
    [void](Test-JsemStaged -Name 'jsem.img' -Bytes $L.RegionBytes -Md5 $img.Md5Image -Code 41 -Label 'the staged image' -After 'T2')

    # THE WRITE WINDOW (see .NOTES). From the flag below to the end of gate 18 every
    # Fail re-reads the anchors and prints the restore command (Fail's post-write
    # hook), and the finally prints it for anything else that leaves the try -- a
    # Ctrl+C, a hung ssh, an unhandled error -- and exits 8.
    $script:jsemA0 = $a0
    $script:jsemL = $L
    $script:jsemMode = 'project'
    $script:jsemLocalPre = $localPre
    $script:jsemLeaf = $preName
    $script:jsemBlockDone = $false
    try {
        Say ''
        Say '== gates 12-13: the write, records then header =='
        $script:jsemWritten = $true
        $w1 = Invoke-Box -RemoteCommand $plan[0]
        if ($w1.Code -ne 0) { Fail 51 ('the RECORDS write FAILED (exit {0}); the header was not written, so the box still reads the old header. The pre-image is retained: -ProjectRestore puts it back.' -f $w1.Code) }
        Pass 'records written (conv=fsync,notrunc)'
        $w2 = Invoke-Box -RemoteCommand $plan[1]
        if ($w2.Code -ne 0) { Fail 51 ('the HEADER write FAILED (exit {0}) after the records were written. The pre-image is retained: -ProjectRestore puts it back.' -f $w2.Code) }
        Pass 'header written (conv=fsync,notrunc)'

        Say ''
        Say '== gate 14: sync and drop_caches =='
        $dc2 = Invoke-Box -RemoteCommand (Get-CmdDropCaches)
        if ($dc2.Code -ne 0) { Fail 95 ('drop_caches FAILED after the write (exit {0}) -- the readback would not be trustworthy' -f $dc2.Code) }
        Pass 'caches dropped (the iflag=direct read below carries the guarantee; this is belt-and-braces)'

        Say ''
        Say '== gates 15-16: the readback, from the device (R1/R2) =='
        $r1 = Invoke-Box -RemoteCommand (Get-CmdRangeToFile -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount -Name 'jsem_post.bin')
        Add-Artifact -Path ('{0}:~/jsem_post.bin' -f $BoxHost) -What 'the readback of the region after the write, on the box' -Staged -Name 'jsem_post.bin'
        $rsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name 'jsem_post.bin')
        if ($r1.Code -ne 0 -or $rsz.Code -ne 0 -or $rsz.Out -ne [string]$L.RegionBytes) {
            Fail 96 ("R1 the readback is '{0}' bytes (read exit {1}), expected {2}" -f $rsz.Out, $r1.Code, $L.RegionBytes)
        }
        Pass ('R1 readback {0} bytes (iflag=direct)' -f $rsz.Out)
        $rw = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'whole')
        $rh = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'header')
        $rr = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'records')
        if ($rw.Out -ne $img.Md5Image -or $rh.Out -ne $img.Md5Header -or $rr.Out -ne $img.Md5Records) {
            $cmp = Invoke-Box -RemoteCommand (Get-CmdCmpHead -A 'jsem.img' -B 'jsem_post.bin')
            Say '  cmp -l jsem.img jsem_post.bin | head -n 20 (offset, octal image byte, octal device byte):'
            foreach ($ln in @($cmp.Out -split "`n")) { Say ('    {0}' -f $ln) }
            # Which side moved: the staged source, or the device?
            $sm = Invoke-Box -RemoteCommand (Get-CmdBoxFileMd5 -Name 'jsem.img')
            if ($sm.Code -eq 0 -and $sm.Out -eq $img.Md5Image) {
                Info ('the staged source ~/jsem.img still reads md5 {0}, the image''s: the source did not change, so the DEVICE holds other bytes' -f $sm.Out)
            } else {
                Warn ("the staged source ~/jsem.img now reads md5 '{0}' (exit {1}), NOT the image's {2}: the SOURCE changed after its re-verify, and the cmp above compares the device with that changed file" -f $sm.Out, $sm.Code, $img.Md5Image)
            }
            Fail 61 ('R2 the readback md5s (whole {0}, header {1}, records {2}) are not the manifest''s ({3}, {4}, {5}). The pre-image is retained: -ProjectRestore puts it back.' -f $rw.Out, $rh.Out, $rr.Out, $img.Md5Image, $img.Md5Header, $img.Md5Records)
        }
        Pass ('R2 the device reads back whole {0}, header {1}, records {2} -- the manifest''s' -f $rw.Out, $rh.Out, $rr.Out)

        Say ''
        Say '== gate 17: the anchors AFTER the write (A1) =='
        $a1 = Read-JsemAnchors -L $L -ReadFailCode 62 -When 'after'
        [void](Compare-JsemAnchors -Before $a0 -After $a1)

        Say ''
        Say '== gate 18: parse_semantic.py reads the readback on the box (P) =='
        $ps = Invoke-Box -RemoteCommand (Get-CmdParseSemantic -BoxRepo $BoxRepo -Name 'jsem_post.bin')
        $count = -1
        if ($ps.Code -eq 0) {
            try { $count = @(($ps.Out | ConvertFrom-Json).records).Count } catch { $count = -1 }
        }
        if ($ps.Code -ne 0 -or $count -ne [int]$img.N) {
            Fail 99 ('P parse_semantic.py read {0} records (exit {1}); the manifest''s n is {2}' -f $count, $ps.Code, $img.N)
        }
        Pass ('P parse_semantic.py reads {0} records off the device -- the manifest''s n' -f $count)
        $script:jsemBlockDone = $true
    } catch {
        # An unhandled error: named here, because the finally's exit would swallow it.
        # A Fail's exit never lands here -- a catch does not see an exit.
        Write-Host ('  FAIL: an unhandled error inside the JSEM write window: {0}' -f $_.Exception.Message) -ForegroundColor Red
        throw
    } finally {
        if ($script:jsemWritten -and -not $script:jsemBlockDone -and -not $script:jsemBannerShown) {
            Show-JsemWrittenBanner
            Write-Transcript -Command 'ABORT: interrupted inside the JSEM write window (the device may hold part of the image)' -ExitCode 'interrupted'
            Show-Artifacts
            exit 8
        }
    }

    Say ''
    Say '== gate 19: cleanup (the pre-image stays on BOTH hosts) =='
    $gone = $true
    foreach ($nm in @('jsem.img', 'jsem_post.bin')) {
        if (Remove-BoxFile -Names @($nm)) { Remove-Artifact -Path ('{0}:~/{1}' -f $BoxHost, $nm) } else { $gone = $false }
    }
    if ($gone) { Pass '~/jsem.img and ~/jsem_post.bin removed from the box, proven absent' }
    Say ('  pre-image kept : {0}' -f $localPre)
    Say ('                   {0}:~/{1}' -f $BoxHost, $preName)
    Say ('  its md5        : {0}' -f $lmd)
    Say ('  A0 sidecar     : {0}' -f $sidePath)
    Say '  -ProjectRestore writes it back; keep it.'
    Write-Transcript -Command ('project complete: image {0}, pre-image {1} md5 {2}' -f $img.Md5Image, $preName, $lmd) -ExitCode 0
    exit 0
}

# ===========================================================================
# -ProjectRestore -- write a retained pre-image back, with the same discipline.
# Above the -Rekey header for the same reason as -Project, and every path exits.
# ===========================================================================
if ($ProjectRestore) {
    Say '== jarvis_admin.ps1 -ProjectRestore =='

    Say ''
    Say '== P1 and the constants =='
    [void](Test-JsemTools)
    $L = Get-JsemLayoutChecked
    [void](Test-JsemBoxReachable)

    Say ''
    Say '== the pre-image, on this PC and on the box =='
    $src = $PreImage
    if (-not $src) {
        $cands = @(Get-ChildItem -LiteralPath $adminHome -Filter 'jsem_pre_*.bin' -File -ErrorAction SilentlyContinue |
                   Where-Object { Test-JsemPreImageName -Leaf $_.Name } | Sort-Object Name)
        if ($cands.Count -eq 0) { Fail 97 ("no -PreImage given, and no jsem_pre_<UTC stamp>.bin in '{0}'" -f $adminHome) }
        $src = $cands[$cands.Count - 1].FullName
        Info ('-PreImage resolved to the newest by the UTC stamp in its name: {0}' -f $src)
    }
    # The leaf is interpolated into root commands on the box, so its shape is
    # checked HOWEVER it was resolved, an explicit -PreImage included.
    $leaf = Split-Path -Leaf $src
    if (-not (Test-JsemPreImageName -Leaf $leaf)) {
        Fail 97 ("the pre-image name '{0}' is not jsem_pre_<yyyyMMddTHHmmssZ>.bin -- it is used in commands run as root on the box, so only the name -Project writes is accepted" -f $leaf)
    }
    if (-not (Test-Path -LiteralPath $src)) { Fail 97 ("the pre-image '{0}' does not exist" -f $src) }
    $lsz = (Get-Item -LiteralPath $src).Length
    $lmd = Get-FileMd5 -Path $src
    if ([uint64]$lsz -ne $L.RegionBytes) { Fail 97 ("the pre-image '{0}' is {1} bytes, expected {2}" -f $src, $lsz, $L.RegionBytes) }
    $bsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name $leaf)
    $bmd = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $leaf -Part 'whole')
    if ($bsz.Code -ne 0 -or $bsz.Out -ne [string]$L.RegionBytes -or $bmd.Code -ne 0 -or $bmd.Out -ne $lmd) {
        # Never pushed by this script: which copy is right is the operator's call.
        $push = Get-CmdScpTo -BoxHost $BoxHost -Dir (Split-Path -Parent $src) -LocalName $leaf -RemoteName $leaf
        Fail 97 ("the box copy ~/{0} is '{1}' bytes, md5 '{2}'; this PC's is {3} bytes, md5 {4} -- both must exist and agree. To put this PC's copy on the box, run this yourself, then re-run: {5}" -f $leaf, $bsz.Out, $bmd.Out, $lsz, $lmd, $push)
    }
    $ph = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $leaf -Part 'header')
    $pr = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $leaf -Part 'records')
    if ($ph.Code -ne 0 -or $pr.Code -ne 0) { Fail 97 ('the pre-image slices could not be hashed on the box (exits {0}, {1})' -f $ph.Code, $pr.Code) }
    Pass ('pre-image {0}: {1} B, md5 whole {2}, header {3}, records {4} -- identical on both hosts' -f $leaf, $lsz, $lmd, $ph.Out, $pr.Out)

    Say ''
    Say '== drop_caches, proven before the write =='
    $dc = Invoke-Box -RemoteCommand (Get-CmdDropCaches)
    if ($dc.Code -ne 0) { Fail 95 ('drop_caches FAILED (exit {0}) -- nothing was written' -f $dc.Code) }
    Pass ('drop_caches ran: {0}' -f (Get-CmdDropCaches))

    Say ''
    Say '== the anchors BEFORE the write =='
    $a0 = Read-JsemAnchors -L $L -ReadFailCode 62 -When 'before'
    # The A0 sidecar the -Project run wrote beside this pre-image, if any: its
    # anchors beside the ones read now. A difference is WARNED, never failed --
    # whether to restore is the operator's decision.
    $sidePath = Join-Path (Split-Path -Parent $src) ($leaf + '.anchors.txt')
    if (Test-Path -LiteralPath $sidePath) {
        $side = $null
        try { $side = ConvertFrom-JsemAnchorLines -Lines ([IO.File]::ReadAllLines($sidePath)) }
        catch { Warn ("the anchors sidecar '{0}' could not be read: {1}" -f $sidePath, $_.Exception.Message) }
        if ($null -ne $side) {
            $sStamp = if ($side.Contains('stamp')) { $side['stamp'] } else { '(absent)' }
            $sMd5 = if ($side.Contains('region_md5')) { $side['region_md5'] } else { '(absent)' }
            Say ('  the -Project run that made this pre-image recorded (stamp {0}, region md5 {1}):' -f $sStamp, $sMd5)
            $diff = $false
            foreach ($k in $a0.Keys) {
                $sv = if ($side.Contains($k)) { $side[$k] } else { '(absent)' }
                if ($sv -eq $a0[$k]) { $mark = 'equal' } else { $mark = 'DIFFERS'; $diff = $true }
                Info ('anchor {0,-22} sidecar {1}  now {2}  {3}' -f $k, $sv, $a0[$k], $mark)
            }
            if ($diff) {
                Warn 'an anchor differs from the -Project run that made this pre-image. If JARVIS has booted since, that is expected: the episodic store and JACT are written at every boot. If it has not, a neighbouring store was damaged -- stop and assess before restoring.'
            }
        }
    } else {
        Info ('no anchors sidecar beside the pre-image ({0}) -- there is nothing to compare the anchors with' -f $sidePath)
    }
    $cur = Invoke-Box -RemoteCommand (Get-CmdRangeMd5 -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount)
    Info ('the region md5 now: {0}' -f $cur.Out)
    if ($cur.Code -eq 0 -and $cur.Out -eq $lmd) {
        Warn ('the region already holds this pre-image (md5 {0}) -- the restore would change nothing. The pre-images on this PC:' -f $lmd)
        foreach ($c in @(Get-ChildItem -LiteralPath $adminHome -Filter 'jsem_pre_*.bin' -File -ErrorAction SilentlyContinue |
                         Where-Object { Test-JsemPreImageName -Leaf $_.Name } | Sort-Object Name)) {
            Info ('{0}  md5 {1}' -f $c.FullName, (Get-FileMd5 -Path $c.FullName))
        }
    }

    $plan = @(Get-JsemWritePlan -Device $BoxDevice -Name $leaf -BaseLba $L.BaseLba -MaxFacts $L.MaxFacts)
    Say ''
    Say '=========================== THE JSEM RESTORE ==============================='
    Say ('  device       : {0}' -f $BoxDevice)
    Say ('  region       : LBA {0} for {1} sectors ({2} bytes)' -f $L.BaseLba, $L.RegionCount, $L.RegionBytes)
    Say ('  source       : {0} (box copy ~/{1}), md5 {2}' -f $src, $leaf, $lmd)
    Say ('  region now   : {0}' -f $cur.Out)
    Say ('  write 1 of 2 : {0}' -f $plan[0])
    Say ('  write 2 of 2 : {0}' -f $plan[1])
    Say '============================================================================='
    $answer = Read-Host ('  type {0} to write, anything else to abort' -f $ProjectRestoreWord)
    if (-not ("$answer".Trim() -ceq $ProjectRestoreWord)) {
        Say '  not confirmed -- NOTHING was written.'
        Write-Transcript -Command 'project restore NOT confirmed' -ExitCode 50
        exit 50
    }

    Say ''
    Say '== the box copy, re-verified AFTER the typed word (the prompt can wait indefinitely) =='
    [void](Test-JsemStaged -Name $leaf -Bytes $L.RegionBytes -Md5 $lmd -Code 97 -Label 'the box copy of the pre-image' -After 'its pre-write check' -Slice)

    # THE WRITE WINDOW, as in -Project: from the flag to the after-anchor comparison.
    $script:jsemA0 = $a0
    $script:jsemL = $L
    $script:jsemMode = 'restore'
    $script:jsemLocalPre = $src
    $script:jsemLeaf = $leaf
    $script:jsemBlockDone = $false
    try {
        $script:jsemWritten = $true
        $w1 = Invoke-Box -RemoteCommand $plan[0]
        if ($w1.Code -ne 0) { Fail 51 ('the RECORDS restore write FAILED (exit {0})' -f $w1.Code) }
        Pass 'records written (conv=fsync,notrunc)'
        $w2 = Invoke-Box -RemoteCommand $plan[1]
        if ($w2.Code -ne 0) { Fail 51 ('the HEADER restore write FAILED (exit {0}) after the records were written' -f $w2.Code) }
        Pass 'header written (conv=fsync,notrunc)'

        $dc2 = Invoke-Box -RemoteCommand (Get-CmdDropCaches)
        if ($dc2.Code -ne 0) { Fail 95 ('drop_caches FAILED after the write (exit {0})' -f $dc2.Code) }
        Pass 'caches dropped'

        $r1 = Invoke-Box -RemoteCommand (Get-CmdRangeToFile -Device $BoxDevice -Lba $L.BaseLba -Count $L.RegionCount -Name 'jsem_restore_post.bin')
        Add-Artifact -Path ('{0}:~/jsem_restore_post.bin' -f $BoxHost) -What 'the readback of the region after the restore, on the box' -Staged -Name 'jsem_restore_post.bin'
        $rsz = Invoke-Box -RemoteCommand (Get-CmdBoxFileSize -Name 'jsem_restore_post.bin')
        if ($r1.Code -ne 0 -or $rsz.Code -ne 0 -or $rsz.Out -ne [string]$L.RegionBytes) {
            Fail 96 ("the restore readback is '{0}' bytes (read exit {1}), expected {2}" -f $rsz.Out, $r1.Code, $L.RegionBytes)
        }
        $rw = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_restore_post.bin' -Part 'whole')
        $rh = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_restore_post.bin' -Part 'header')
        $rr = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name 'jsem_restore_post.bin' -Part 'records')
        if ($rw.Out -ne $lmd -or $rh.Out -ne $ph.Out -or $rr.Out -ne $pr.Out) {
            $cmp = Invoke-Box -RemoteCommand (Get-CmdCmpHead -A $leaf -B 'jsem_restore_post.bin')
            Say ('  cmp -l {0} jsem_restore_post.bin | head -n 20:' -f $leaf)
            foreach ($ln in @($cmp.Out -split "`n")) { Say ('    {0}' -f $ln) }
            # Which side moved: the box copy of the source, or the device?
            $sm = Invoke-Box -RemoteCommand (Get-CmdFileSliceMd5 -Name $leaf -Part 'whole')
            if ($sm.Code -eq 0 -and $sm.Out -eq $lmd) {
                Info ('the source ~/{0} still reads md5 {1}, this PC''s copy: the source did not change, so the DEVICE holds other bytes' -f $leaf, $sm.Out)
            } else {
                Warn ("the source ~/{0} now reads md5 '{1}' (exit {2}), NOT this PC's {3}: the SOURCE changed after its re-verify, and the cmp above compares the device with that changed file" -f $leaf, $sm.Out, $sm.Code, $lmd)
            }
            Fail 98 ('the restore readback md5s (whole {0}, header {1}, records {2}) are not the pre-image''s ({3}, {4}, {5})' -f $rw.Out, $rh.Out, $rr.Out, $lmd, $ph.Out, $pr.Out)
        }
        Pass ('the device reads back the pre-image: whole {0}, header {1}, records {2}' -f $rw.Out, $rh.Out, $rr.Out)

        $a1 = Read-JsemAnchors -L $L -ReadFailCode 62 -When 'after'
        [void](Compare-JsemAnchors -Before $a0 -After $a1)
        $script:jsemBlockDone = $true
    } catch {
        # An unhandled error: named here, because the finally's exit would swallow it.
        # A Fail's exit never lands here -- a catch does not see an exit.
        Write-Host ('  FAIL: an unhandled error inside the JSEM write window: {0}' -f $_.Exception.Message) -ForegroundColor Red
        throw
    } finally {
        if ($script:jsemWritten -and -not $script:jsemBlockDone -and -not $script:jsemBannerShown) {
            Show-JsemWrittenBanner
            Write-Transcript -Command 'ABORT: interrupted inside the JSEM write window (the device may hold part of the image)' -ExitCode 'interrupted'
            Show-Artifacts
            exit 8
        }
    }

    if (Remove-BoxFile -Names @('jsem_restore_post.bin')) {
        Remove-Artifact -Path ('{0}:~/jsem_restore_post.bin' -f $BoxHost)
        Pass '~/jsem_restore_post.bin removed, proven absent (the pre-image stays)'
    }
    Write-Transcript -Command ('project restore complete: region = pre-image {0} md5 {1}' -f $leaf, $lmd) -ExitCode 0
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
