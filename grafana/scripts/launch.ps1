<#
.SYNOPSIS
    Launch HFT Grafana dashboard stack on Windows.
.DESCRIPTION
    Starts InfluxDB + Grafana via Docker Compose, ingests analytics data,
    and opens the dashboard in the default browser.
.PARAMETER Replay
    Run the replay binary first to generate fresh analytics data.
.EXAMPLE
    .\grafana\scripts\launch.ps1
    .\grafana\scripts\launch.ps1 -Replay
#>
param(
    [switch]$Replay
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path "$ScriptDir\..\..").Path
$ComposeFile = Join-Path $ProjectRoot "grafana\docker-compose.yml"
$CsvPath = Join-Path $ProjectRoot "data\analytics.csv"
$JsonPath = Join-Path $ProjectRoot "data\analytics.json"

Write-Host "=== HFT Orderbook Grafana Dashboard ===" -ForegroundColor Cyan
Write-Host ""

# Step 1: Optionally run replay
if ($Replay) {
    Write-Host "[1/5] Running replay to generate analytics data..." -ForegroundColor Yellow
    $ReplayBin = Join-Path $ProjectRoot "build\Release\replay.exe"
    if (-not (Test-Path $ReplayBin)) {
        $ReplayBin = Join-Path $ProjectRoot "build\replay.exe"
    }
    if (-not (Test-Path $ReplayBin)) {
        Write-Host "ERROR: Replay binary not found." -ForegroundColor Red
        Write-Host "  Build first: cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build"
        exit 1
    }
    $InputCsv = Join-Path $ProjectRoot "data\btcusdt_l3_sample.csv"
    & $ReplayBin --input $InputCsv --analytics --analytics-csv $CsvPath --analytics-json $JsonPath
    Write-Host ""
} else {
    Write-Host "[1/5] Skipping replay (use -Replay to generate fresh data)" -ForegroundColor DarkGray
}

# Step 2: Verify analytics files
Write-Host "[2/5] Checking analytics data files..." -ForegroundColor Yellow
if (-not (Test-Path $CsvPath)) {
    Write-Host "ERROR: Analytics CSV not found: $CsvPath" -ForegroundColor Red
    Write-Host "  Run with -Replay flag, or generate manually:"
    Write-Host "  .\build\replay.exe --input data\btcusdt_l3_sample.csv --analytics ``"
    Write-Host "    --analytics-csv data\analytics.csv --analytics-json data\analytics.json"
    exit 1
}
$HasJson = Test-Path $JsonPath
if (-not $HasJson) {
    Write-Host "WARNING: Analytics JSON not found (summary stats will be empty)" -ForegroundColor DarkYellow
}
Write-Host "  CSV:  $CsvPath"
Write-Host "  JSON: $JsonPath"
Write-Host ""

# Step 3: Start containers
Write-Host "[3/5] Starting InfluxDB + Grafana containers..." -ForegroundColor Yellow
docker compose -f $ComposeFile up -d
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Docker Compose failed. Is Docker Desktop running?" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Step 4: Install Python deps
Write-Host "[4/5] Installing Python dependencies..." -ForegroundColor Yellow
$ReqFile = Join-Path $ScriptDir "requirements.txt"
pip install -q -r $ReqFile
Write-Host ""

# Step 5: Ingest data
Write-Host "[5/5] Ingesting analytics data into InfluxDB..." -ForegroundColor Yellow
$IngestScript = Join-Path $ScriptDir "ingest.py"
$IngestArgs = @("--csv", $CsvPath, "--drop")
if ($HasJson) {
    $IngestArgs += @("--json", $JsonPath)
}
python $IngestScript @IngestArgs
Write-Host ""

Write-Host "=========================================" -ForegroundColor Green
Write-Host "  Dashboard ready: http://localhost:3000" -ForegroundColor Green
Write-Host "  InfluxDB UI:     http://localhost:8086" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Teardown: docker compose -f grafana/docker-compose.yml down -v" -ForegroundColor DarkGray

# Open browser
Start-Process "http://localhost:3000"
