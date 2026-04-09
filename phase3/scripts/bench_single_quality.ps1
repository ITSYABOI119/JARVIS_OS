# JARVIS AI-OS — Single Model Quality Bench (Windows/Main PC)
# Usage: powershell -ExecutionPolicy Bypass -File phase3\scripts\bench_single_quality.ps1 -Model "path\to\model.gguf"

param(
    [Parameter(Mandatory=$true)]
    [string]$Model
)

$LlamaCLI = "$HOME\llama.cpp\build\bin\Release\llama-completion.exe"
$ResultsDir = "C:\Users\jluca\Documents\JARVIS_OS\models\quality_results"

if (-not (Test-Path $LlamaCLI)) {
    Write-Host "ERROR: llama-completion.exe not found"
    exit 1
}

if (-not (Test-Path $Model)) {
    Write-Host "ERROR: Model not found: $Model"
    exit 1
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

$Name = [System.IO.Path]::GetFileNameWithoutExtension($Model)
$OutFile = "$ResultsDir\$Name.txt"

$Prompts = @(
    "The seL4 microkernel is"
    "Explain how virtual memory works in three sentences."
    "What is the difference between a process and a thread?"
    "Write a C function that reverses a string in place."
    "The capital of Australia is"
    "What are the key benefits of formally verified software?"
    "Describe the TCP three-way handshake step by step."
    "What happens when you type a URL into a browser?"
    "Compare RISC and CISC architectures in two sentences."
    "Write a bash one-liner to find all .c files larger than 1MB."
)

Write-Host "=== Quality Bench: $Name ==="
Write-Host "Date: $(Get-Date)"
Write-Host "Prompts: $($Prompts.Count)"
Write-Host ""

$FileOutput = @()
$FileOutput += "=== $Name ==="
$FileOutput += "Date: $(Get-Date)"
$FileOutput += ""

for ($i = 0; $i -lt $Prompts.Count; $i++) {
    $P = $Prompts[$i]
    $PN = $i + 1

    Write-Host "  Prompt $PN..."

    $FileOutput += "--- Prompt $PN`: `"$P`" ---"

    try {
        $AllOutput = & $LlamaCLI -m $Model -p $P -n 100 --temp 0 -ngl 99 -t 6 -no-cnv --no-warmup 2>&1
        $Response = ($AllOutput | Where-Object {
            $_ -notmatch "^(ggml_|llama_|load|print_info|sched_|common_|main:|system_info|sampler|generate)" -and
            $_ -notmatch "^\s*$" -and
            $_ -notmatch "^\.+$" -and
            $_ -notmatch "memory breakdown"
        } | Out-String).Trim()
        $FileOutput += $Response
    } catch {
        $FileOutput += "[FAILED TO GENERATE]"
    }

    $FileOutput += ""
}

$FileOutput += "=== End $Name ==="
$FileOutput | Out-File -FilePath $OutFile -Encoding utf8

Write-Host ""
Write-Host "Saved to $OutFile"
Write-Host ""

# Also append to ALL_RESPONSES.txt for easy comparison
$AllFile = "$ResultsDir\ALL_RESPONSES.txt"
if (Test-Path $AllFile) {
    $Existing = Get-Content $AllFile -Raw
    # Remove the closing line if present
    $Existing = $Existing -replace "=== End All Responses ===\s*$", ""

    $Append = @()
    $Append += ""
    $Append += "=========================================="
    $Append += "ADDITIONAL MODEL: $Name"
    $Append += "=========================================="

    for ($i = 0; $i -lt $Prompts.Count; $i++) {
        $P = $Prompts[$i]
        $PN = $i + 1

        $Lines = Get-Content $OutFile
        $Capturing = $false
        $Response = @()

        foreach ($line in $Lines) {
            if ($line -match "--- Prompt $PN`:") {
                $Capturing = $true
                continue
            }
            if ($Capturing -and ($line -match "--- Prompt \d" -or $line -match "=== End")) {
                break
            }
            if ($Capturing) {
                $Response += $line
            }
        }

        $Append += ""
        $Append += "PROMPT $PN`: `"$P`""
        $Append += "[$Name]"
        $Append += ($Response -join "`n").Trim()
        $Append += ""
    }

    $Append += "=== End All Responses ==="

    ($Existing + ($Append -join "`n")) | Out-File -FilePath $AllFile -Encoding utf8
    Write-Host "Appended to $AllFile"
}
