# JARVIS AI-OS — Quality Bench (Windows/Main PC with GPU)
# Runs 10 prompts through each model, saves responses for comparison
# Usage: powershell -ExecutionPolicy Bypass -File phase3\scripts\bench_quality_windows.ps1

$ModelsDir = "C:\Users\jluca\Documents\JARVIS_OS\models"
$LlamaCLI = "$HOME\llama.cpp\build\bin\Release\llama-completion.exe"
$ResultsDir = "$ModelsDir\quality_results"

if (-not (Test-Path $LlamaCLI)) {
    Write-Host "ERROR: llama-completion.exe not found at $LlamaCLI"
    exit 1
}

New-Item -ItemType Directory -Force -Path $ResultsDir | Out-Null

# 10 test prompts — factual, reasoning, code, OS-domain
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

$Models = Get-ChildItem "$ModelsDir\*.gguf" | Sort-Object Name
$Count = $Models.Count

Write-Host "=== JARVIS AI-OS Quality Bench ==="
Write-Host "Date: $(Get-Date)"
Write-Host "Models: $Count"
Write-Host "Prompts: $($Prompts.Count)"
Write-Host "Config: greedy (temp=0), 100 tokens, GPU offload"
Write-Host ""

$N = 0
foreach ($m in $Models) {
    $N++
    $Name = $m.BaseName
    $OutFile = "$ResultsDir\$Name.txt"

    Write-Host "[$N/$Count] $Name"

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
            $AllOutput = & $LlamaCLI -m $m.FullName -p $P -n 100 --temp 0 -ngl 99 -t 6 -no-cnv --no-warmup 2>&1
            # Filter out llama.cpp log lines, keep only the generated text
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

    Write-Host "  Saved to $OutFile"
    Write-Host ""
}

# Generate a combined file for easy Claude judging
$CombinedFile = "$ResultsDir\ALL_RESPONSES.txt"
$Combined = @()
$Combined += "=== JARVIS AI-OS Quality Bench - All Models ==="
$Combined += "Date: $(Get-Date)"
$Combined += "Prompts: $($Prompts.Count) | Models: $Count | Config: greedy, 100 tokens"
$Combined += ""

for ($i = 0; $i -lt $Prompts.Count; $i++) {
    $P = $Prompts[$i]
    $PN = $i + 1
    $Combined += "========================================"
    $Combined += "PROMPT $PN`: `"$P`""
    $Combined += "========================================"
    $Combined += ""

    foreach ($m in $Models) {
        $Name = $m.BaseName
        $OutFile = "$ResultsDir\$Name.txt"

        if (Test-Path $OutFile) {
            $Content = Get-Content $OutFile -Raw
            # Extract response for this prompt
            $Pattern = "--- Prompt $PN`:.*?---"
            $NextPattern = "--- Prompt $($PN+1)`:.*?---|=== End"

            # Simple extraction: find the prompt header and grab lines until next prompt or end
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

            $Combined += "[$Name]"
            $Combined += ($Response -join "`n").Trim()
            $Combined += ""
        }
    }

    $Combined += ""
}

$Combined += "=== End All Responses ==="
$Combined | Out-File -FilePath $CombinedFile -Encoding utf8

Write-Host "=== Quality Bench Complete ==="
Write-Host "Per-model: $ResultsDir\(model).txt"
Write-Host "Combined:  $CombinedFile (paste into Claude for judging)"
