# JARVIS AI-OS — Perplexity Bench (Windows/Main PC with GPU)
# Full WikiText-2 corpus, no chunk limit — GPU makes it fast enough
# Usage: powershell -ExecutionPolicy Bypass -File phase3\scripts\bench_perplexity_windows.ps1

$ModelsDir = "C:\Users\jluca\Documents\JARVIS_OS\models"
$LlamaPerplexity = "$HOME\llama.cpp\build\bin\Release\llama-perplexity.exe"
$WikiText = "$ModelsDir\wikitext-2-raw\wiki.test.raw"
$ResultsFile = "$ModelsDir\perplexity_results.txt"

if (-not (Test-Path $LlamaPerplexity)) {
    Write-Host "ERROR: llama-perplexity.exe not found at $LlamaPerplexity"
    Write-Host "Build with CUDA: cd ~/llama.cpp && cmake -B build -DGGML_CUDA=ON && cmake --build build --config Release -j 12"
    exit 1
}

if (-not (Test-Path $WikiText)) {
    Write-Host "ERROR: WikiText-2 not found at $WikiText"
    Write-Host "Download: https://huggingface.co/datasets/ggml-org/ci/resolve/main/wikitext-2-raw-v1.zip"
    exit 1
}

$GPU = try { (Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match "NVIDIA|AMD|Radeon" }).Name } catch { "None" }
$CorpusSize = [math]::Round((Get-Item $WikiText).Length / 1MB, 1)

$Header = @"
=== JARVIS AI-OS Perplexity Bench (Full WikiText-2) ===
Date: $(Get-Date)
Hostname: $env:COMPUTERNAME
CPU: $((Get-CimInstance Win32_Processor).Name)
GPU: $GPU
RAM: $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB))GB
Corpus: wiki.test.raw (${CorpusSize}MB) — FULL, no chunk limit
Config: -ngl 99 (GPU offload), -t 6
"@

Write-Host $Header
Write-Host ""

$Models = Get-ChildItem "$ModelsDir\*.gguf" | Sort-Object Name
$Count = $Models.Count

Write-Host "Found $Count models:"
foreach ($m in $Models) {
    $Size = [math]::Round($m.Length / 1GB, 1)
    Write-Host "  ${Size}GB  $($m.Name)"
}
Write-Host ""

$Output = @()
$Output += $Header
$Output += ""

$N = 0
foreach ($m in $Models) {
    $N++
    $Name = $m.Name
    Write-Host "=== [$N/$Count] $Name ==="
    $Output += "=== [$N/$Count] $Name ==="

    $StartTime = Get-Date

    try {
        $Result = & $LlamaPerplexity -m $m.FullName -f $WikiText -ngl 99 -t 6 2>&1
        # Get the last 5 lines which contain the final PPL
        $Tail = $Result | Select-Object -Last 5
        $Tail | ForEach-Object {
            Write-Host $_
            $Output += $_
        }
    } catch {
        $Err = "  FAILED: $Name"
        Write-Host $Err
        $Output += $Err
    }

    $Elapsed = [math]::Round(((Get-Date) - $StartTime).TotalMinutes, 1)
    $TimeStr = "  Time: ${Elapsed} min"
    Write-Host $TimeStr
    $Output += $TimeStr
    Write-Host ""
    $Output += ""
}

$Output += "=== Perplexity Bench Complete ==="

$Output | Out-File -FilePath $ResultsFile -Encoding utf8
Write-Host ""
Write-Host "Results saved to $ResultsFile"
