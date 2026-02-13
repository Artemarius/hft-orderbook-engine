#!/usr/bin/env python3
"""Ingest HFT analytics CSV and JSON into InfluxDB for Grafana visualization.

Usage:
    python ingest.py --csv data/analytics.csv --json data/analytics.json
    python ingest.py --csv data/analytics.csv --json data/analytics.json --drop
"""

import argparse
import csv
import json
import sys
import time
from pathlib import Path

try:
    from influxdb_client import InfluxDBClient, Point, WritePrecision
    from influxdb_client.client.write_api import SYNCHRONOUS
except ImportError:
    print("ERROR: influxdb-client not installed.")
    print("  pip install influxdb-client>=1.36.0")
    sys.exit(1)


DEFAULT_URL = "http://localhost:8086"
DEFAULT_TOKEN = "hft-dev-token"
DEFAULT_ORG = "hft-engine"
DEFAULT_BUCKET = "analytics"


def wait_for_influxdb(url, retries=30, delay=2):
    """Poll InfluxDB health endpoint until ready."""
    import urllib.request
    import urllib.error

    health_url = f"{url}/health"
    for i in range(retries):
        try:
            req = urllib.request.Request(health_url)
            with urllib.request.urlopen(req, timeout=3) as resp:
                if resp.status == 200:
                    return True
        except (urllib.error.URLError, ConnectionError, OSError):
            pass
        if i < retries - 1:
            print(f"  Waiting for InfluxDB... ({i + 1}/{retries})")
            time.sleep(delay)
    return False


def drop_and_recreate_bucket(client, org, bucket_name):
    """Delete and recreate the analytics bucket for a clean start."""
    buckets_api = client.buckets_api()
    orgs_api = client.organizations_api()

    org_list = orgs_api.find_organizations(org=org)
    if not org_list:
        print(f"ERROR: Organization '{org}' not found.")
        sys.exit(1)
    org_id = org_list[0].id

    existing = buckets_api.find_bucket_by_name(bucket_name)
    if existing:
        print(f"  Dropping bucket '{bucket_name}'...")
        buckets_api.delete_bucket(existing)

    print(f"  Creating bucket '{bucket_name}'...")
    from influxdb_client.domain.bucket_retention_rules import BucketRetentionRules
    retention = BucketRetentionRules(type="expire", every_seconds=0)
    buckets_api.create_bucket(
        bucket_name=bucket_name,
        retention_rules=retention,
        org_id=org_id,
    )


def ingest_csv(write_api, bucket, org, csv_path):
    """Ingest analytics CSV into the 'trades' measurement."""
    print(f"\nIngesting CSV: {csv_path}")

    float_fields = [
        "trade_price", "spread", "spread_bps", "microprice",
        "imbalance", "tick_vol", "depth_imbalance",
    ]
    int_fields = ["trade_quantity", "sequence_num"]

    points = []
    row_count = 0

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ts = int(row["timestamp"])
            p = Point("trades").time(ts, WritePrecision.NS)

            for field in float_fields:
                val = row.get(field, "")
                if val:
                    p = p.field(field, float(val))

            for field in int_fields:
                val = row.get(field, "")
                if val:
                    p = p.field(field, int(val))

            aggressor = row.get("aggressor_side", "")
            if aggressor:
                p = p.tag("aggressor_side", aggressor)

            points.append(p)
            row_count += 1

            if len(points) >= 500:
                write_api.write(bucket=bucket, org=org, record=points)
                points = []

    if points:
        write_api.write(bucket=bucket, org=org, record=points)

    print(f"  Wrote {row_count} trade points.")
    return row_count


def ingest_json(write_api, bucket, org, json_path):
    """Ingest analytics JSON into 'summary' and 'depth_profile' measurements."""
    print(f"\nIngesting JSON: {json_path}")

    with open(json_path, "r") as f:
        data = json.load(f)

    # Use a fixed reference timestamp for summary data
    summary_ts = 1704067200000000000  # 2024-01-01T00:00:00Z in nanoseconds

    # -- Summary measurement: one point with all aggregate metrics --
    p = Point("summary").time(summary_ts, WritePrecision.NS)

    if "trade_count" in data:
        p = p.field("trade_count", float(data["trade_count"]))

    spread = data.get("spread", {})
    for key in ["current_spread_bps", "avg_spread_bps", "min_spread_bps",
                "max_spread_bps", "avg_effective_spread_bps"]:
        if key in spread:
            p = p.field(key, float(spread[key]))
    if "spread_samples" in spread:
        p = p.field("spread_samples", float(spread["spread_samples"]))

    microprice = data.get("microprice", {})
    if "microprice" in microprice:
        p = p.field("microprice", float(microprice["microprice"]))

    ofi = data.get("order_flow_imbalance", {})
    for key in ["current_imbalance", "buy_volume", "sell_volume"]:
        if key in ofi:
            p = p.field(key, float(ofi[key]))

    rv = data.get("realized_volatility", {})
    for key in ["tick_volatility", "tick_return_count",
                "time_bar_volatility", "time_bar_count"]:
        if key in rv:
            p = p.field(key, float(rv[key]))

    impact = data.get("price_impact", {})
    for key in ["kyle_lambda", "avg_temporary_impact_bps",
                "avg_permanent_impact_bps", "sample_count"]:
        val = impact.get(key)
        if val is not None:
            p = p.field(key, float(val))

    write_api.write(bucket=bucket, org=org, record=p)
    print("  Wrote 1 summary point.")

    # -- Depth profile measurement: one point per level per side --
    depth = data.get("depth_profile", {})
    depth_points = []

    for side_key, side_tag in [("current_bid_depth", "bid"),
                                ("current_ask_depth", "ask")]:
        levels = depth.get(side_key, [])
        avg_key = f"avg_{side_tag}_depth"
        avg_levels = depth.get(avg_key, [])

        for i, qty in enumerate(levels):
            dp = (Point("depth_profile")
                  .time(summary_ts, WritePrecision.NS)
                  .tag("side", side_tag)
                  .tag("level", str(i))
                  .field("current_quantity", float(qty)))
            if i < len(avg_levels):
                dp = dp.field("avg_quantity", float(avg_levels[i]))
            depth_points.append(dp)

    if depth_points:
        write_api.write(bucket=bucket, org=org, record=depth_points)
        print(f"  Wrote {len(depth_points)} depth profile points.")


def main():
    parser = argparse.ArgumentParser(
        description="Ingest HFT analytics data into InfluxDB"
    )
    parser.add_argument("--csv", help="Path to analytics CSV file")
    parser.add_argument("--json", help="Path to analytics JSON file")
    parser.add_argument("--url", default=DEFAULT_URL, help="InfluxDB URL")
    parser.add_argument("--token", default=DEFAULT_TOKEN, help="InfluxDB token")
    parser.add_argument("--org", default=DEFAULT_ORG, help="InfluxDB org")
    parser.add_argument("--bucket", default=DEFAULT_BUCKET, help="InfluxDB bucket")
    parser.add_argument("--drop", action="store_true",
                        help="Drop and recreate the bucket before ingesting")
    args = parser.parse_args()

    if not args.csv and not args.json:
        parser.error("At least one of --csv or --json is required.")

    if args.csv and not Path(args.csv).exists():
        print(f"ERROR: CSV file not found: {args.csv}")
        print("  Run the replay first:")
        print("  ./build/replay --input data/btcusdt_l3_sample.csv --analytics "
              "--analytics-csv data/analytics.csv --analytics-json data/analytics.json")
        sys.exit(1)

    if args.json and not Path(args.json).exists():
        print(f"ERROR: JSON file not found: {args.json}")
        print("  Run the replay first:")
        print("  ./build/replay --input data/btcusdt_l3_sample.csv --analytics "
              "--analytics-csv data/analytics.csv --analytics-json data/analytics.json")
        sys.exit(1)

    print(f"Connecting to InfluxDB at {args.url}...")
    if not wait_for_influxdb(args.url, retries=15, delay=2):
        print("ERROR: InfluxDB is not reachable.")
        print("  Start containers: docker compose -f grafana/docker-compose.yml up -d")
        sys.exit(1)

    client = InfluxDBClient(url=args.url, token=args.token, org=args.org)

    if args.drop:
        drop_and_recreate_bucket(client, args.org, args.bucket)

    write_api = client.write_api(write_options=SYNCHRONOUS)

    total = 0
    if args.csv:
        total += ingest_csv(write_api, args.bucket, args.org, args.csv)
    if args.json:
        ingest_json(write_api, args.bucket, args.org, args.json)

    client.close()
    print(f"\nDone. Ingested {total} trade points into '{args.bucket}'.")
    print("Open Grafana: http://localhost:3000")


if __name__ == "__main__":
    main()
