<#
.SYNOPSIS
  Unit test for jarvis_admin.ps1's JSEM projection (Phase 7 MS3b-1): the pure
  builders, the layout, the write plan, the forwarding, and the placement of the
  -Project and -ProjectRestore blocks. Never touches a device, a box or ssh.

.DESCRIPTION
  jarvis_admin.ps1's body RUNS on load, so it is never dot-sourced here. Its AST
  is parsed with [Parser]::ParseFile, and only the functions this test calls are
  defined, each from its own FunctionDefinitionAst extent:

    the ten builders  Get-CmdRangeMd5 Get-CmdRangeToFile Get-CmdImageWrite
                      Get-JsemWritePlan Get-CmdDropCaches Get-CmdFileSliceMd5
                      Get-CmdCmpHead Get-CmdParseSemantic Get-CmdScpTo
                      Get-CmdScpFrom
    the pure helpers  Get-JsemLayout Get-ForwardArgs
                      Get-JsemWrittenBanner Test-JsemPreImageName
                      ConvertTo-JsemAnchorLines ConvertFrom-JsemAnchorLines
    what they call    Get-HeaderConst Get-LeMagicHex Get-CmdBoxFileMd5

  It also asserts, statically over the AST, the JSEM write window (MS3b-1 fix):
  Fail's post-write hook, the flag's single placement in each JSEM block, the
  staged re-verify before it, each block's finally, and -Check's slice, parse
  and pull legs.

  They run under Set-StrictMode -Version 2.0, so a builder that read a
  script-scope variable (such as $BoxDevice) would throw here instead of passing.

  Runs under Windows PowerShell 5.1 and under pwsh on Linux (the CI step
  "Phase C: jarvis_admin JSEM builders and layout"). Paths are built with
  Join-Path and forward slashes, and no Windows-only API is used.

  Prints N/N checks passed; exits non-zero on any failure.
#>
Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:checks = 0
$script:fails = 0
function Test-That {
    param([string]$Name, [bool]$Cond, [string]$Detail = '')
    $script:checks++
    if ($Cond) { Write-Output ('PASS {0}' -f $Name) }
    else { $script:fails++; Write-Output ('FAIL {0} {1}' -f $Name, $Detail) }
}

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$adminPath = Join-Path $repo 'phasec/scripts/jarvis_admin.ps1'
$selfPath = Join-Path $repo 'phasec/scripts/test_jarvis_admin_jsem.ps1'
$semH = Join-Path $repo 'phase3/src/ai/semantic_store.h'
$epiH = Join-Path $repo 'phase3/src/ai/episodic_store.h'
$actH = Join-Path $repo 'phase3/src/ai/action_audit.h'

# --- 1. the parse must be clean before anything else is believed ---------------
# ParseFile does not throw on an error: a path that does not exist returns an AST
# with no statements and one error. So the error list is the gate.
$tokens = $null
$errs = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($adminPath, [ref]$tokens, [ref]$errs)
$nErr = @($errs).Count
Test-That 'T1 jarvis_admin.ps1 parses with no errors' ($nErr -eq 0) (($errs | ForEach-Object { $_.Message }) -join '; ')
if ($nErr -ne 0 -or @($ast.EndBlock.Statements).Count -eq 0) {
    Write-Output ('{0}/{1} checks passed' -f ($script:checks - $script:fails), $script:checks)
    exit 1
}

# --- 2. both files are pure ASCII ------------------------------------------------
# Windows PowerShell 5.1 reads a .ps1 without a BOM as ANSI, so a UTF-8 dash in
# a string becomes a curly quote that ENDS the string and breaks the parse; the
# A4 parse gate runs under pwsh, which reads UTF-8, and cannot see it.
foreach ($p in @($adminPath, $selfPath)) {
    $bytes = [System.IO.File]::ReadAllBytes($p)
    $hi = 0
    foreach ($x in $bytes) { if ($x -gt 0x7F) { $hi++ } }
    Test-That ('T2 {0} is pure ASCII ({1} bytes)' -f (Split-Path -Leaf $p), $bytes.Length) ($hi -eq 0) ('{0} bytes above 0x7F' -f $hi)
}

# --- 3. define the functions under test from their AST extents -------------------
$want = @('Get-HeaderConst', 'Get-LeMagicHex', 'Get-CmdBoxFileMd5', 'Get-JsemLayout', 'Get-ForwardArgs',
          'Get-CmdRangeMd5', 'Get-CmdRangeToFile', 'Get-CmdImageWrite', 'Get-JsemWritePlan', 'Get-CmdDropCaches',
          'Get-CmdFileSliceMd5', 'Get-CmdCmpHead', 'Get-CmdParseSemantic', 'Get-CmdScpTo', 'Get-CmdScpFrom',
          'Get-JsemWrittenBanner', 'Test-JsemPreImageName', 'ConvertTo-JsemAnchorLines', 'ConvertFrom-JsemAnchorLines')
$fnAsts = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true))
$missing = @()
foreach ($name in $want) {
    $defs = @($fnAsts | Where-Object { $_.Name -eq $name })
    if ($defs.Count -ne 1) { $missing += ('{0} x{1}' -f $name, $defs.Count); continue }
    . ([scriptblock]::Create($defs[0].Extent.Text))
}
Test-That ('T3 each of the {0} functions under test is defined exactly once in jarvis_admin.ps1, and extracted' -f $want.Count) ($missing.Count -eq 0) ($missing -join ', ')
Write-Output ('     extracted: {0}' -f ($want -join ' '))

# --- 4. the layout ----------------------------------------------------------------
$L = Get-JsemLayout -SemHeader $semH -EpiHeader $epiH -ActHeader $actH
Test-That 'T4a layout: JSEM base 21110000, records 21110001 for 4096, region 4097 sectors = 2,097,664 bytes' `
    ($L.BaseLba -eq 21110000 -and $L.RecordsLba -eq 21110001 -and $L.MaxFacts -eq 4096 -and $L.RegionCount -eq 4097 -and $L.RegionBytes -eq 2097664) `
    ('{0} {1} {2} {3} {4}' -f $L.BaseLba, $L.RecordsLba, $L.MaxFacts, $L.RegionCount, $L.RegionBytes)
Test-That 'T4b layout: the episodic header 21100000 and its tail 21108177 (the last 16 record slots), the JACT head 21120000, 16 sectors each' `
    ($L.EpiHeaderLba -eq 21100000 -and $L.EpiTailLba -eq 21108177 -and $L.ActHeadLba -eq 21120000 -and $L.AnchorCount -eq 16) `
    ('{0} {1} {2} {3}' -f $L.EpiHeaderLba, $L.EpiTailLba, $L.ActHeadLba, $L.AnchorCount)
Test-That 'T4c layout: the little-endian magics JSEM 4d45534a, JEPI 4950454a, JACT 5443414a; version 1' `
    ($L.SemMagicHex -eq '4d45534a' -and $L.EpiMagicHex -eq '4950454a' -and $L.ActMagicHex -eq '5443414a' -and $L.SemVersion -eq 1) `
    ('{0} {1} {2} {3}' -f $L.SemMagicHex, $L.EpiMagicHex, $L.ActMagicHex, $L.SemVersion)

# --- 5. the builders: golden strings and the properties each must hold -----------
$dev = '/dev/nvme0n1'
$g = [ordered]@{
    rangeMd5    = @((Get-CmdRangeMd5 -Device $dev -Lba 21110000 -Count 4097),
                    'set -o pipefail; sudo -n dd if=/dev/nvme0n1 bs=512 skip=21110000 count=4097 iflag=direct status=none | md5sum | cut -c1-32')
    rangeToFile = @((Get-CmdRangeToFile -Device $dev -Lba 21110000 -Count 4097 -Name 'jsem_post.bin'),
                    'set -o pipefail; sudo -n dd if=/dev/nvme0n1 bs=512 skip=21110000 count=4097 iflag=direct status=none > $HOME/jsem_post.bin')
    imageWrite  = @((Get-CmdImageWrite -Device $dev -Name 'jsem.img' -InSkip 1 -Seek 21110001 -Count 4096),
                    'sudo -n dd if=$HOME/jsem.img of=/dev/nvme0n1 bs=512 skip=1 seek=21110001 count=4096 conv=fsync,notrunc status=none')
    dropCaches  = @((Get-CmdDropCaches), 'sync; sudo -n sysctl -q vm.drop_caches=3')
    sliceHeader = @((Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'header'),
                    'set -o pipefail; head -c 512 $HOME/jsem_post.bin | md5sum | cut -c1-32')
    sliceRecs   = @((Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'records'),
                    'set -o pipefail; tail -c +513 $HOME/jsem_post.bin | md5sum | cut -c1-32')
    sliceWhole  = @((Get-CmdFileSliceMd5 -Name 'jsem_post.bin' -Part 'whole'),
                    'set -o pipefail; md5sum $HOME/jsem_post.bin | cut -c1-32')
    cmpHead     = @((Get-CmdCmpHead -A 'jsem.img' -B 'jsem_post.bin'), 'cmp -l $HOME/jsem.img $HOME/jsem_post.bin | head -n 20')
    parseSem    = @((Get-CmdParseSemantic -BoxRepo '$HOME/Desktop/JARVIS_OS' -Name 'jsem_post.bin'),
                    'set -o pipefail; python3 $HOME/Desktop/JARVIS_OS/phase3/scripts/parse_semantic.py --json $HOME/jsem_post.bin')
    scpTo       = @((Get-CmdScpTo -BoxHost 'jarvis' -Dir 'C:\x' -LocalName 'jsem.img' -RemoteName 'jsem.img'), 'cd C:\x; scp jsem.img jarvis:jsem.img')
    scpFrom     = @((Get-CmdScpFrom -BoxHost 'jarvis' -Dir 'C:\x' -RemoteName 'jsem_pre_x.bin' -LocalName 'jsem_pre_x.bin'),
                    'cd C:\x; scp jarvis:jsem_pre_x.bin jsem_pre_x.bin')
}
foreach ($k in $g.Keys) {
    Test-That ('T5 {0}: the exact golden string' -f $k) ($g[$k][0] -ceq $g[$k][1]) ("got '{0}'" -f $g[$k][0])
}
$all = @($g.Keys | ForEach-Object { $g[$_][0] })
$dq = @($all | Where-Object { $_.Contains('"') })
Test-That 'T5a no builder string contains a double-quote character' ($dq.Count -eq 0) ($dq -join ' | ')
$dds = @($all | Where-Object { $_ -match '\bdd\b' })
$nobs = @($dds | Where-Object { $_ -notmatch '(^|\s)bs=512(\s|$)' })
Test-That ('T5b every dd ({0} of them) carries bs=512' -f $dds.Count) ($dds.Count -ge 3 -and $nobs.Count -eq 0) ($nobs -join ' | ')
$wr = $g['imageWrite'][0]
Test-That 'T5c the write reads its input with skip= and writes the device with seek=' ($wr -match '\bif=\$HOME/\S+' -and $wr -match '\bskip=1\b' -and $wr -match '\bof=/dev/nvme0n1\b' -and $wr -match '\bseek=21110001\b') $wr
Test-That 'T5d the write carries conv=fsync,notrunc' ($wr -match '\bconv=fsync,notrunc\b') $wr
Test-That 'T5e the write carries fsync (named on its own)' ($wr -match 'conv=\S*\bfsync\b') $wr
Test-That 'T5f the write carries notrunc (named on its own: without it the header write truncates a FILE target)' ($wr -match 'conv=\S*\bnotrunc\b') $wr
$reads = @($g['rangeMd5'][0], $g['rangeToFile'][0])
Test-That 'T5g iflag=direct in both device reads (Get-CmdRangeMd5, Get-CmdRangeToFile)' (@($reads | Where-Object { $_ -notmatch '\biflag=direct\b' }).Count -eq 0) ($reads -join ' | ')
$files = @($g['sliceHeader'][0], $g['sliceRecs'][0], $g['sliceWhole'][0], $g['cmpHead'][0])
Test-That 'T5h iflag=direct absent from the file readers' (@($files | Where-Object { $_ -match 'iflag' }).Count -eq 0) ($files -join ' | ')
$md5p = @($g['rangeMd5'][0], $g['sliceHeader'][0], $g['sliceRecs'][0], $g['sliceWhole'][0])
Test-That 'T5i set -o pipefail leads every md5 pipeline' (@($md5p | Where-Object { -not $_.StartsWith('set -o pipefail;') }).Count -eq 0) ($md5p -join ' | ')
Test-That 'T5j the cmp shape is capped with head -n 20' ($g['cmpHead'][0] -match '\| head -n 20$') $g['cmpHead'][0]
$dcs = $g['dropCaches'][0]
Test-That 'T5k the drop-caches shape uses sysctl and no > redirect (the shell doing a redirect is not root)' ($dcs -match '\bsysctl\b' -and -not $dcs.Contains('>')) $dcs

# --- 6. the plan -----------------------------------------------------------------
$plan = @(Get-JsemWritePlan -Device $dev -Name 'jsem.img' -BaseLba 21110000 -MaxFacts 4096)
Test-That 'T6a the plan is exactly two writes' ($plan.Count -eq 2) ('{0}' -f $plan.Count)
Test-That 'T6b records FIRST: skip=1 seek=21110001 count=4096' ($plan.Count -ge 1 -and $plan[0] -match 'skip=1 seek=21110001 count=4096') ($plan -join ' | ')
Test-That 'T6c header LAST: skip=0 seek=21110000 count=1' ($plan.Count -ge 2 -and $plan[1] -match 'skip=0 seek=21110000 count=1') ($plan -join ' | ')
Test-That 'T6d the plan is the builder, verbatim' ($plan.Count -eq 2 -and $plan[0] -ceq $g['imageWrite'][1] -and $plan[1] -ceq 'sudo -n dd if=$HOME/jsem.img of=/dev/nvme0n1 bs=512 skip=0 seek=21110000 count=1 conv=fsync,notrunc status=none') ($plan -join ' | ')
$plan0 = @(Get-JsemWritePlan -Device '$HOME/jsem_probe.out' -Name 'jsem_probe.img' -BaseLba 0 -MaxFacts 4096)
Test-That 'T6e at base 0 (-Check''s file rehearsal): seek=1, then seek=0' ($plan0.Count -eq 2 -and $plan0[0] -match '\bseek=1\b' -and $plan0[1] -match '\bseek=0\b') ($plan0 -join ' | ')

# --- 7. the forwarding --------------------------------------------------------------
$fa = @(Get-ForwardArgs -Params ([ordered]@{ A = 'x'; B = ''; C = 'has space'; D = 'd' }))
Test-That 'T7a Get-ForwardArgs drops an empty value, keeps a value with a space as ONE element, and keeps the order' `
    ($fa.Count -eq 6 -and ($fa -join '|') -ceq '-A|x|-C|has space|-D|d') ('{0}: {1}' -f $fa.Count, ($fa -join '|'))
$fe = @(Get-ForwardArgs -Params ([ordered]@{ PreImage = '' }))
Test-That 'T7b an all-empty map forwards nothing' ($fe.Count -eq 0) ('{0}' -f $fe.Count)

$menu = @($fnAsts | Where-Object { $_.Name -eq 'Invoke-Menu' })
$declared = @($ast.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
$wantKeys = @('KeyFile', 'BoxHost', 'Port', 'Image', 'Manifest', 'ExpectPreMd5', 'ExpectImageMd5', 'PreImage', 'BoxRepo')
if ($menu.Count -eq 1) {
    $hts = @($menu[0].FindAll({ param($n) $n -is [System.Management.Automation.Language.HashtableAst] }, $true) |
             Where-Object { @($_.KeyValuePairs | ForEach-Object { $_.Item1.Extent.Text }) -contains 'KeyFile' })
    $keys = if ($hts.Count -eq 1) { @($hts[0].KeyValuePairs | ForEach-Object { $_.Item1.Extent.Text }) } else { @() }
    Test-That 'T7c Invoke-Menu holds exactly one map carrying KeyFile, with exactly the nine forwarded keys in order' `
        ($hts.Count -eq 1 -and ($keys -join ',') -ceq ($wantKeys -join ',')) ('{0} map(s): {1}' -f $hts.Count, ($keys -join ','))
    $undeclared = @($keys | Where-Object { $declared -notcontains $_ })
    Test-That 'T7d every forwarded key is a parameter the param block declares' ($keys.Count -gt 0 -and $undeclared.Count -eq 0) ($undeclared -join ',')
    $calls = @($menu[0].FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Get-ForwardArgs' }, $true))
    Test-That 'T7e Invoke-Menu calls Get-ForwardArgs' ($calls.Count -ge 1) ('{0}' -f $calls.Count)
    $lits = @($menu[0].FindAll({ param($n) ($n -is [System.Management.Automation.Language.ArrayLiteralAst]) -or ($n -is [System.Management.Automation.Language.ArrayExpressionAst]) }, $true) |
              Where-Object { @($_.FindAll({ param($m) $m -is [System.Management.Automation.Language.StringConstantExpressionAst] -and $m.Value -eq '-KeyFile' }, $true)).Count -gt 0 })
    Test-That 'T7f no array literal in Invoke-Menu still carries the string -KeyFile' ($lits.Count -eq 0) ('{0}' -f $lits.Count)
} else {
    Test-That 'T7c Invoke-Menu is defined exactly once' $false ('{0}' -f $menu.Count)
}

# --- 8. the placement, statically ---------------------------------------------------
# No runtime check can see a missing exit: Fail exits on every abort path, and
# nothing here runs the success path. So the AST is the proof.
$top = @($ast.EndBlock.Statements)
$anchor = @($top | Where-Object { $_.Extent.Text -eq "Say '== jarvis_admin.ps1 -Rekey =='" })
Test-That 'T8a the -Rekey anchor statement exists exactly once at top level' ($anchor.Count -eq 1) ('{0}' -f $anchor.Count)
$ifs = @($top | Where-Object { $_ -is [System.Management.Automation.Language.IfStatementAst] })
foreach ($cond in @('$Project', '$ProjectRestore')) {
    $blk = @($ifs | Where-Object { $_.Clauses[0].Item1.Extent.Text -eq $cond })
    Test-That ('T8b the top-level if ({0}) block exists exactly once' -f $cond) ($blk.Count -eq 1) ('{0}' -f $blk.Count)
    if ($blk.Count -eq 1 -and $anchor.Count -eq 1) {
        $stmts = @($blk[0].Clauses[0].Item2.Statements)
        $last = $stmts[$stmts.Count - 1]
        Test-That ('T8c the {0} block ends above the -Rekey anchor (block lines {1}-{2}, anchor line {3})' -f $cond, $blk[0].Extent.StartLineNumber, $blk[0].Extent.EndLineNumber, $anchor[0].Extent.StartLineNumber) `
            ($blk[0].Extent.EndLineNumber -lt $anchor[0].Extent.StartLineNumber)
        Test-That ('T8d the {0} block''s last statement is an exit (line {1}: {2})' -f $cond, $last.Extent.StartLineNumber, $last.Extent.Text) `
            ($last -is [System.Management.Automation.Language.ExitStatementAst])
    }
}
$plans = @($fnAsts | Where-Object { $_.Name -eq 'Get-JsemWritePlan' })
$writes = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Get-CmdImageWrite' }, $true))
$outside = @($writes | Where-Object {
    $p = $_.Parent
    $inPlan = $false
    while ($null -ne $p) {
        if ($p -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $p.Name -eq 'Get-JsemWritePlan') { $inPlan = $true; break }
        $p = $p.Parent
    }
    -not $inPlan
})
Test-That ('T8e exactly two Get-CmdImageWrite calls in the script, both inside Get-JsemWritePlan (found {0}, {1} outside)' -f $writes.Count, $outside.Count) `
    ($plans.Count -eq 1 -and $writes.Count -eq 2 -and $outside.Count -eq 0) (($outside | ForEach-Object { 'line {0}' -f $_.Extent.StartLineNumber }) -join ', ')

# --- 9. the MS3b-1 fix's pure helpers -------------------------------------------------
$bp = Get-JsemWrittenBanner -Mode 'project' -LocalPre 'C:\a\jsem_pre_20260926T093819Z.bin' -BoxHost 'jarvis' -Leaf 'jsem_pre_20260926T093819Z.bin'
Test-That 'T9a the project banner, golden' ($bp -ceq 'THE JSEM REGION HAS BEEN WRITTEN, possibly only in part. The pre-image is kept at C:\a\jsem_pre_20260926T093819Z.bin and jarvis:~/jsem_pre_20260926T093819Z.bin. Restore it with: jarvis_admin.bat -ProjectRestore -PreImage C:\a\jsem_pre_20260926T093819Z.bin') $bp
$br = Get-JsemWrittenBanner -Mode 'restore' -LocalPre 'C:\a\jsem_pre_20260926T093819Z.bin' -BoxHost 'jarvis' -Leaf 'jsem_pre_20260926T093819Z.bin'
Test-That 'T9b the restore banner, golden' ($br -ceq 'THE RESTORE WRITE HAS STARTED, and the region may hold a mix of old and new bytes. The source is still at C:\a\jsem_pre_20260926T093819Z.bin and jarvis:~/jsem_pre_20260926T093819Z.bin. Re-run: jarvis_admin.bat -ProjectRestore -PreImage C:\a\jsem_pre_20260926T093819Z.bin') $br
Test-That 'T9c Test-JsemPreImageName accepts jsem_pre_20260926T093819Z.bin' (Test-JsemPreImageName -Leaf 'jsem_pre_20260926T093819Z.bin')
$badNames = @('jsem_pre_x.bin', 'a b.bin', 'jsem_pre_20260926T093819Z.bin;rm', '../jsem_pre_20260926T093819Z.bin')
$accepted = @($badNames | Where-Object { Test-JsemPreImageName -Leaf $_ })
Test-That ('T9d Test-JsemPreImageName refuses all {0}: {1}' -f $badNames.Count, ($badNames -join ' | ')) ($accepted.Count -eq 0) ('accepted: ' + ($accepted -join ' | '))
$sx = [ordered]@{
    'episodic header md5'   = '066d7a49681b65c049c3c8488eb2604d'
    'episodic header magic' = '4950454a'
    'episodic tail md5'     = '682941ce1951db355ee17229efe08413'
    'JACT head md5'         = '2207fe2105a5891177e2dc982c1a6439'
    'JACT head magic'       = '5443414a'
    'stamp'                 = '20260926T093819Z'
    'region_md5'            = '1365c929581e56c8bbc59308feeab8e3'
}
$sLines = @(ConvertTo-JsemAnchorLines $sx)
$sBack = ConvertFrom-JsemAnchorLines $sLines
$sSame = ((@($sBack.Keys) -join '|') -ceq (@($sx.Keys) -join '|')) -and (@($sx.Keys | Where-Object { $sBack[$_] -cne $sx[$_] }).Count -eq 0)
$sText = $sLines -join "`n"
$sHi = @([Text.Encoding]::UTF8.GetBytes($sText) | Where-Object { $_ -gt 0x7F }).Count
Test-That ('T9e the sidecar round-trips five anchors, the stamp and the region md5 ({0} lines), and its text is pure ASCII' -f $sLines.Count) ($sLines.Count -eq 7 -and $sSame -and $sHi -eq 0) $sText

# --- 10. the JSEM write window, statically ----------------------------------------------
function Get-VarRefs {
    param($Node, [string]$Name)
    @($Node.FindAll({ param($n) $n -is [System.Management.Automation.Language.VariableExpressionAst] -and $n.VariablePath.UserPath -eq $Name }, $true))
}
function Test-IsFlagSet {
    param($n, [string]$Value)
    ($n -is [System.Management.Automation.Language.AssignmentStatementAst]) -and $n.Left.Extent.Text -eq '$script:jsemWritten' -and $n.Right.Extent.Text -eq $Value
}
$failFn = @($fnAsts | Where-Object { $_.Name -eq 'Fail' })
$hookOk = ($failFn.Count -eq 1) -and (@(Get-VarRefs $failFn[0] 'script:jsemWritten').Count -ge 1) -and (@(Get-VarRefs $failFn[0] 'script:jsemInAnchorCheck').Count -ge 1)
Test-That 'T10a Fail''s body references $script:jsemWritten and $script:jsemInAnchorCheck (the post-write hook)' $hookOk ('{0} Fail definition(s)' -f $failFn.Count)

$allFlagTrue = @($ast.FindAll({ param($n) Test-IsFlagSet $n '$true' }, $true))
$allFlagFalse = @($ast.FindAll({ param($n) Test-IsFlagSet $n '$false' }, $true))
$blocks = @()
foreach ($cond in @('$Project', '$ProjectRestore')) {
    $blk = @($ifs | Where-Object { $_.Clauses[0].Item1.Extent.Text -eq $cond })
    if ($blk.Count -ne 1) { Test-That ('T10b the {0} block exists exactly once' -f $cond) $false; continue }
    $b = $blk[0]
    $blocks += $b
    $rh = @($b.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Read-Host' }, $true))
    $fl = @($b.FindAll({ param($n) Test-IsFlagSet $n '$true' }, $true))
    $w0 = @($b.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Invoke-Box' -and $n.Extent.Text -match '-RemoteCommand\s+\$plan\[0\]' }, $true))
    $placed = ($rh.Count -eq 1 -and $fl.Count -eq 1 -and $w0.Count -eq 1) -and
              ($rh[0].Extent.StartOffset -lt $fl[0].Extent.StartOffset) -and ($fl[0].Extent.StartOffset -lt $w0[0].Extent.StartOffset)
    Test-That ('T10b in the {0} block $script:jsemWritten = $true appears exactly once, after its one Read-Host and before its one Invoke-Box -RemoteCommand $plan[0]' -f $cond) $placed `
        ('Read-Host x{0}, flag x{1}, plan[0] write x{2}' -f $rh.Count, $fl.Count, $w0.Count)
    $rv = @($b.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Test-JsemStaged' }, $true))
    $between = ($rh.Count -eq 1 -and $fl.Count -eq 1) -and @($rv | Where-Object { $_.Extent.StartOffset -gt $rh[0].Extent.StartOffset -and $_.Extent.StartOffset -lt $fl[0].Extent.StartOffset }).Count -ge 1
    Test-That ('T10c in the {0} block a Test-JsemStaged re-verify lies between the Read-Host and the flag' -f $cond) $between ('Test-JsemStaged x{0}' -f $rv.Count)
    $fins = @($b.FindAll({ param($n) $n -is [System.Management.Automation.Language.TryStatementAst] -and $null -ne $n.Finally }, $true))
    $finOk = @($fins | Where-Object { @(Get-VarRefs $_.Finally 'script:jsemWritten').Count -ge 1 -and @(Get-VarRefs $_.Finally 'script:jsemBannerShown').Count -ge 1 }).Count -ge 1
    Test-That ('T10d the {0} block has a finally that references $script:jsemWritten and $script:jsemBannerShown' -f $cond) $finOk ('{0} try/finally' -f $fins.Count)
}
$falseOutside = @($allFlagFalse | Where-Object {
    $f = $_
    @($blocks | Where-Object { $f.Extent.StartOffset -ge $_.Extent.StartOffset -and $f.Extent.EndOffset -le $_.Extent.EndOffset }).Count -eq 0
})
$falseTop = @($allFlagFalse | Where-Object { $_.Parent -eq $ast.EndBlock })
Test-That ('T10e $script:jsemWritten = $true is set only in the two JSEM blocks (x{0}), and every = $false (x{1}) lies outside both, at top level' -f $allFlagTrue.Count, $allFlagFalse.Count) `
    ($allFlagTrue.Count -eq 2 -and $allFlagFalse.Count -ge 1 -and $falseOutside.Count -eq $allFlagFalse.Count -and $falseTop.Count -eq $allFlagFalse.Count)

$icp = @($fnAsts | Where-Object { $_.Name -eq 'Invoke-CheckProjection' })
function Test-HasLiteralPart {
    param($Cmd, [string]$Value)
    $els = @($Cmd.CommandElements)
    for ($i = 0; $i -lt $els.Count; $i++) {
        $e = $els[$i]
        if ($e -is [System.Management.Automation.Language.CommandParameterAst] -and $e.ParameterName -eq 'Part') {
            if ($null -ne $e.Argument -and $e.Argument -is [System.Management.Automation.Language.StringConstantExpressionAst] -and $e.Argument.Value -ceq $Value) { return $true }
            if ($i + 1 -lt $els.Count -and $els[$i + 1] -is [System.Management.Automation.Language.StringConstantExpressionAst] -and $els[$i + 1].Value -ceq $Value) { return $true }
        }
    }
    return $false
}
if ($icp.Count -eq 1) {
    $slices = @($icp[0].FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Get-CmdFileSliceMd5' }, $true))
    $hasH = @($slices | Where-Object { Test-HasLiteralPart $_ 'header' }).Count -ge 1
    $hasR = @($slices | Where-Object { Test-HasLiteralPart $_ 'records' }).Count -ge 1
    Test-That 'T10f Invoke-CheckProjection calls Get-CmdFileSliceMd5 with a literal -Part header and a literal -Part records' ($hasH -and $hasR) ('header {0}, records {1}' -f $hasH, $hasR)
    $cPs = @($icp[0].FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Get-CmdParseSemantic' }, $true)).Count
    $cRx = @($icp[0].FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.GetCommandName() -eq 'Receive-FromBox' }, $true)).Count
    Test-That 'T10g Invoke-CheckProjection calls Get-CmdParseSemantic and Receive-FromBox' ($cPs -ge 1 -and $cRx -ge 1) ('parse x{0}, pull x{1}' -f $cPs, $cRx)
} else {
    Test-That 'T10f Invoke-CheckProjection is defined exactly once' $false ('{0}' -f $icp.Count)
}

Write-Output ''
Write-Output ('{0}/{1} checks passed' -f ($script:checks - $script:fails), $script:checks)
if ($script:fails -gt 0) { exit 1 }
exit 0
