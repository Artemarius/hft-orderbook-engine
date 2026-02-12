"""Analytics demo: replay with full market microstructure analytics.

Runs a single-instrument replay, feeds events to the analytics engine,
then prints summary stats and writes JSON/CSV output.

Usage:
    python python/examples/analytics_demo.py
"""

import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
python_dir = os.path.dirname(script_dir)
sys.path.insert(0, python_dir)

import hft_orderbook as hft


def main():
    repo_root = os.path.dirname(python_dir)
    data_path = os.path.join(repo_root, "data", "btcusdt_l3_sample.csv")

    if not os.path.isfile(data_path):
        print(f"Error: sample data not found at {data_path}")
        sys.exit(1)

    # Configure replay.
    config = hft.ReplayConfig()
    config.input_path = data_path
    config.min_price = hft.float_to_price(41000.0)
    config.max_price = hft.float_to_price(43000.0)
    config.tick_size = hft.float_to_price(0.01)
    config.enable_publisher = True  # Required for event callbacks

    # Create replay engine.
    engine = hft.ReplayEngine(config)

    # Create analytics engine with custom config.
    analytics_config = hft.AnalyticsConfig()
    analytics_config.imbalance_window = 100
    analytics_config.vol_tick_window = 50
    analytics_config.depth_max_levels = 10

    analytics = hft.AnalyticsEngine(engine.order_book, analytics_config)

    # Wire analytics to receive events directly in C++ (no Python overhead).
    engine.register_analytics(analytics)

    # Run replay.
    stats = engine.run()

    print(f"=== Replay: {stats.total_messages} messages, "
          f"{stats.trades_generated} trades ===\n")

    # Print analytics summary (calls C++ print_summary).
    analytics.print_summary()

    # Access individual module results.
    print("\n=== Individual Module Access ===")
    print(f"  Spread:     avg={analytics.spread.avg_spread_bps():.2f} bps")
    print(f"  Microprice: {analytics.microprice.current_microprice():.2f} "
          f"(valid={analytics.microprice.is_valid()})")
    print(f"  Imbalance:  {analytics.order_flow.current_imbalance():.4f} "
          f"({analytics.order_flow.sample_count()} samples)")
    print(f"  Tick vol:   {analytics.volatility.tick_volatility():.6f}")
    print(f"  Kyle lambda:{analytics.price_impact.kyle_lambda():.6f}")
    print(f"  Depth imb:  {analytics.depth.depth_imbalance():.4f}")

    # Get full analytics as a Python dict.
    results = analytics.to_dict()
    print(f"\n  Analytics dict keys: {list(results.keys())}")

    # Write outputs.
    output_dir = os.path.join(repo_root, "build")
    os.makedirs(output_dir, exist_ok=True)

    json_path = os.path.join(output_dir, "analytics_output.json")
    csv_path = os.path.join(output_dir, "analytics_timeseries.csv")
    analytics.write_json(json_path)
    analytics.write_csv(csv_path)
    print(f"\n  JSON written to: {json_path}")
    print(f"  CSV written to:  {csv_path}")


if __name__ == "__main__":
    main()
