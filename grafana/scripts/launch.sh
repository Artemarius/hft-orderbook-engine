#!/usr/bin/env bash
# Launch HFT Grafana dashboard stack.
# Usage: ./grafana/scripts/launch.sh [--replay]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
COMPOSE_FILE="$PROJECT_ROOT/grafana/docker-compose.yml"
CSV_PATH="$PROJECT_ROOT/data/analytics.csv"
JSON_PATH="$PROJECT_ROOT/data/analytics.json"

REPLAY=false
for arg in "$@"; do
    case "$arg" in
        --replay) REPLAY=true ;;
    esac
done

echo "=== HFT Orderbook Grafana Dashboard ==="
echo ""

# Step 1: Optionally run replay to generate analytics data
if [ "$REPLAY" = true ]; then
    echo "[1/5] Running replay to generate analytics data..."
    REPLAY_BIN="$PROJECT_ROOT/build/replay"
    if [ ! -f "$REPLAY_BIN" ]; then
        echo "ERROR: Replay binary not found at $REPLAY_BIN"
        echo "  Build first: cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build"
        exit 1
    fi
    "$REPLAY_BIN" \
        --input "$PROJECT_ROOT/data/btcusdt_l3_sample.csv" \
        --analytics \
        --analytics-csv "$CSV_PATH" \
        --analytics-json "$JSON_PATH"
    echo ""
else
    echo "[1/5] Skipping replay (use --replay to generate fresh data)"
fi

# Step 2: Verify analytics files exist
echo "[2/5] Checking analytics data files..."
if [ ! -f "$CSV_PATH" ]; then
    echo "ERROR: Analytics CSV not found: $CSV_PATH"
    echo "  Run with --replay flag, or generate manually:"
    echo "  ./build/replay --input data/btcusdt_l3_sample.csv --analytics \\"
    echo "    --analytics-csv data/analytics.csv --analytics-json data/analytics.json"
    exit 1
fi
if [ ! -f "$JSON_PATH" ]; then
    echo "WARNING: Analytics JSON not found: $JSON_PATH (summary stats will be empty)"
fi
echo "  CSV: $CSV_PATH"
echo "  JSON: $JSON_PATH"
echo ""

# Step 3: Start Docker containers
echo "[3/5] Starting InfluxDB + Grafana containers..."
if ! command -v docker &>/dev/null; then
    echo "ERROR: Docker is not installed or not in PATH."
    exit 1
fi
docker compose -f "$COMPOSE_FILE" up -d
echo ""

# Step 4: Wait for InfluxDB and install Python deps
echo "[4/5] Installing Python dependencies and waiting for InfluxDB..."
pip3 install -q -r "$SCRIPT_DIR/requirements.txt"
echo ""

# Step 5: Ingest data
echo "[5/5] Ingesting analytics data into InfluxDB..."
INGEST_ARGS="--csv $CSV_PATH --drop"
if [ -f "$JSON_PATH" ]; then
    INGEST_ARGS="$INGEST_ARGS --json $JSON_PATH"
fi
python3 "$SCRIPT_DIR/ingest.py" $INGEST_ARGS
echo ""

echo "========================================="
echo "  Dashboard ready: http://localhost:3000"
echo "  InfluxDB UI:     http://localhost:8086"
echo "========================================="
echo ""
echo "Teardown: docker compose -f grafana/docker-compose.yml down -v"

# Try to open browser
if command -v xdg-open &>/dev/null; then
    xdg-open "http://localhost:3000" 2>/dev/null || true
elif command -v open &>/dev/null; then
    open "http://localhost:3000" 2>/dev/null || true
fi
