# JARVIS AI-OS — Speed Bench (Windows/Main PC)
# Runs llama-bench on all models — CPU and GPU modes
# Usage: powershell -ExecutionPolicy Bypass -File phase3\scripts\bench_speed_windows.ps1 [-Mode cpu|gpu|both]

param(
    [string]$Mode = "both"
)

$ModelsDir = "C:\Users\jluca\Documents\JARVIS_OS\models"
$LlamaBench = "$HOME\llama.cpp\build\bin\Release\llama-bench.exe"

# Auto-detect physical cores
$PhysCores = (Get-CimInstance Win32_Processor).NumberOfCores
if (-not $PhysCores) { $PhysCores = 6 }

if (-not (Test-Path $LlamaBench)) {
    Write-Host "ERROR: llama-bench.exe not found at $LlamaBench"
    Write-Host "Build it:"
    Write-Host "  cd ~/llama.cpp"
    Write-Host "  cmake -B build -DGGML_CUDA=OFF"
    Write-Host "  cmake --build build --config Release -j 12"
    exit 1
}

$GPU = try { (Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match "NVIDIA|AMD|Radeon" }).Name } catch { "None" }

$Models = Get-ChildItem "$ModelsDir\*.gguf" | Sort-Object Name
$Count = $Models.Count

function Run-Bench($BenchMode, $NGL, $Threads, $ResultsFile) {
    $Header = @"
=== JARVIS AI-OS Speed Bench ($BenchMode) ===
Date: $(Get-Date)
Hostname: $env:COMPUTERNAME
CPU: $((Get-CimInstance Win32_Processor).Name)
GPU: $GPU
RAM: $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB))GB
Threads: $Threads | NGL: $NGL
"@

    Write-Host $Header
    Write-Host ""

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
        Write-Host "[$N/$Count] $Name"
        Write-Host "---"

        $Output += "[$N/$Count] $Name"
        $Output += "---"

        try {
            $Result = & $LlamaBench -m $m.FullName -ngl $NGL -r 1 -p 512 -n 128 -t $Threads 2>&1
            $Result | ForEach-Object {
                Write-Host $_
                $Output += $_
            }
        } catch {
            $Err = "  FAILED: $Name"
            Write-Host $Err
            $Output += $Err
        }

        Write-Host ""
        $Output += ""
    }

    $Output += "=== Speed Bench ($BenchMode) Complete ==="
    $Output | Out-File -FilePath $ResultsFile -Encoding utf8
    Write-Host "Results saved to $ResultsFile"
    Write-Host ""
}

if (-not (Test-Path $LlamaBench)) {
    Write-Host "ERROR: llama-bench.exe not found at $LlamaBench"
    Write-Host "Build with CUDA:"
    Write-Host "  cd ~/llama.cpp"
    Write-Host "  cmake -B build -DGGML_CUDA=ON"
    Write-Host "  cmake --build build --config Release -j 12"
    exit 1
}

switch ($Mode.ToLower()) {
    "cpu" {
        Run-Bench "CPU" 0 $PhysCores "$ModelsDir\bench_results_mainpc_cpu.txt"
    }
    "gpu" {
        Run-Bench "GPU" 99 $PhysCores "$ModelsDir\bench_results_mainpc_gpu.txt"
    }
    "both" {
        Run-Bench "CPU" 0 $PhysCores "$ModelsDir\bench_results_mainpc_cpu.txt"
        Write-Host "=========================================="
        Write-Host ""
        Run-Bench "GPU" 99 $PhysCores "$ModelsDir\bench_results_mainpc_gpu.txt"
    }
    default {
        Write-Host "Usage: .\bench_speed_windows.ps1 [-Mode cpu|gpu|both]"
    }
}
