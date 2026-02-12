# run_benchmarks.ps1 — Build and run all HFT order book benchmarks.
#
# Usage:
#   .\scripts\run_benchmarks.ps1                          # full run
#   .\scripts\run_benchmarks.ps1 -SkipBuild               # skip cmake build
#   .\scripts\run_benchmarks.ps1 -Iterations 100000       # custom iteration count
#   .\scripts\run_benchmarks.ps1 -OutputDir results       # custom output dir

param(
    [string]$BuildDir = "build",
    [string]$BuildType = "Release",
    [int]$Iterations = 1000000,
    [switch]$SkipBuild,
    [string]$OutputDir = "benchmark_results"
)

$ErrorActionPreference = "Stop"

# -----------------------------------------------------------------------
# Locate VS Build Tools
# -----------------------------------------------------------------------

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
}
if (-not $vsPath) {
    $vsPath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
}
$cmakeExe = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"

if (-not (Test-Path $cmakeExe)) {
    Write-Error "CMake not found at $cmakeExe. Install VS Build Tools."
    exit 1
}

# -----------------------------------------------------------------------
# 1. Document environment
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host " Environment" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Write-Host "Date:       $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host "OS:         $([System.Environment]::OSVersion.VersionString)"
Write-Host "CPU:        $($cpu.Name)"
Write-Host "Cores:      $($cpu.NumberOfCores) physical, $($cpu.NumberOfLogicalProcessors) logical"
Write-Host "RAM:        $([math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)) GB"
Write-Host "CMake:      $cmakeExe"
Write-Host "Build type: $BuildType"
Write-Host "Iterations: $Iterations"

# -----------------------------------------------------------------------
# 2. Build (inside vcvarsall environment)
# -----------------------------------------------------------------------

if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host " Building ($BuildType)" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    # Build via a temporary batch file that initializes MSVC environment
    $buildScript = @"
@echo off
call "$vcvarsall" x64 >nul 2>&1
"$cmakeExe" -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType -G Ninja -S . 2>&1
"$cmakeExe" --build $BuildDir 2>&1
exit /b %ERRORLEVEL%
"@
    $tmpBat = [System.IO.Path]::GetTempFileName() + ".bat"
    Set-Content -Path $tmpBat -Value $buildScript -Encoding ASCII
    cmd /c $tmpBat
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed with exit code $LASTEXITCODE"
        Remove-Item $tmpBat -Force
        exit 1
    }
    Remove-Item $tmpBat -Force
    Write-Host "Build succeeded." -ForegroundColor Green
}

# -----------------------------------------------------------------------
# 3. Prepare output directory
# -----------------------------------------------------------------------

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'

# -----------------------------------------------------------------------
# 4. Tips for reproducible results
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host " Tips for Reproducible Results" -ForegroundColor Yellow
Write-Host "================================================================" -ForegroundColor Yellow
Write-Host "  1. Close all other applications"
Write-Host "  2. Set power plan to 'High Performance'"
Write-Host "  3. Run from an elevated (Administrator) PowerShell"
Write-Host "  4. Disable Windows Defender real-time scanning (temporarily)"
Write-Host ""

# -----------------------------------------------------------------------
# 5. Run Google Benchmark executables
# -----------------------------------------------------------------------

$benchExes = @(
    @{ Name = "bench_orderbook"; Path = "$BuildDir\benchmarks\bench_orderbook.exe" },
    @{ Name = "bench_matching";  Path = "$BuildDir\benchmarks\bench_matching.exe" },
    @{ Name = "bench_spsc";      Path = "$BuildDir\benchmarks\bench_spsc.exe" }
)

foreach ($bench in $benchExes) {
    if (Test-Path $bench.Path) {
        Write-Host ""
        Write-Host "================================================================" -ForegroundColor Cyan
        Write-Host " Google Benchmark: $($bench.Name)" -ForegroundColor Cyan
        Write-Host "================================================================" -ForegroundColor Cyan

        $jsonOut = Join-Path $OutputDir "$($bench.Name)_$timestamp.json"
        & $bench.Path `
            --benchmark_format=console `
            --benchmark_out=$jsonOut `
            --benchmark_out_format=json `
            --benchmark_repetitions=5 `
            --benchmark_report_aggregates_only=true

        Write-Host "  JSON saved: $jsonOut" -ForegroundColor DarkGray
    } else {
        Write-Host "  SKIP: $($bench.Path) not found" -ForegroundColor Yellow
    }
}

# -----------------------------------------------------------------------
# 6. Run custom latency histogram benchmark
# -----------------------------------------------------------------------

$latencyExe = "$BuildDir\benchmarks\bench_latency.exe"
if (Test-Path $latencyExe) {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Cyan
    Write-Host " Latency Histogram: bench_latency" -ForegroundColor Cyan
    Write-Host "================================================================" -ForegroundColor Cyan

    $latencyOut = Join-Path $OutputDir "bench_latency_$timestamp.txt"
    & $latencyExe --iterations=$Iterations | Tee-Object -FilePath $latencyOut

    Write-Host "  Results saved: $latencyOut" -ForegroundColor DarkGray
} else {
    Write-Host "  SKIP: $latencyExe not found" -ForegroundColor Yellow
}

# -----------------------------------------------------------------------
# 7. Summary
# -----------------------------------------------------------------------

Write-Host ""
Write-Host "================================================================" -ForegroundColor Green
Write-Host " All benchmarks complete." -ForegroundColor Green
Write-Host " Results saved to: $OutputDir" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Green
