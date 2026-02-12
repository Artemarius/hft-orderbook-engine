"""Multi-instrument replay example.

Replays a multi-instrument L3 CSV with explicit instrument configuration
and per-instrument analytics.

Usage:
    python python/examples/multi_instrument.py
"""

import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
python_dir = os.path.dirname(script_dir)
sys.path.insert(0, python_dir)

import hft_orderbook as hft


def main():
    repo_root = os.path.dirname(python_dir)
    data_path = os.path.join(repo_root, "data", "multi_instrument_l3_sample.csv")

    if not os.path.isfile(data_path):
        print(f"Error: sample data not found at {data_path}")
        sys.exit(1)

    # Configure multi-instrument replay with explicit instruments.
    # (auto_discover creates the router lazily inside run(), so we specify
    # instruments up front to have the router available for analytics wiring.)
    btc = hft.InstrumentConfig()
    btc.instrument_id = 0
    btc.symbol = "BTCUSDT"
    btc.min_price = hft.float_to_price(1.0)
    btc.max_price = hft.float_to_price(100000.0)
    btc.tick_size = hft.float_to_price(0.01)

    eth = hft.InstrumentConfig()
    eth.instrument_id = 1
    eth.symbol = "ETHUSDT"
    eth.min_price = hft.float_to_price(1.0)
    eth.max_price = hft.float_to_price(100000.0)
    eth.tick_size = hft.float_to_price(0.01)

    config = hft.MultiReplayConfig()
    config.input_path = data_path
    config.instruments = [btc, eth]

    # Create engine.
    engine = hft.MultiInstrumentReplayEngine(config)

    # Create per-instrument analytics via the router.
    analytics_config = hft.AnalyticsConfig()
    analytics = hft.MultiInstrumentAnalytics(engine.router(), analytics_config)

    # Wire analytics to receive events directly in C++ (no Python overhead).
    engine.register_analytics(analytics)

    # Run replay.
    stats = engine.run()

    print(f"=== Multi-Instrument Replay ===")
    print(f"  Total messages:  {stats.total_messages}")
    print(f"  Elapsed:         {stats.elapsed_seconds:.3f}s")
    print(f"  Throughput:      {stats.messages_per_second:,.0f} msg/s")
    print(f"  Instruments:     {len(stats.per_instrument)}")

    for ps in stats.per_instrument:
        print(f"\n  --- {ps.symbol} (id={ps.instrument_id}) ---")
        print(f"    Adds: {ps.add_messages}  Cancels: {ps.cancel_messages}  "
              f"Trades: {ps.trades_generated}")
        print(f"    Final orders: {ps.final_order_count}")
        print(f"    Best bid: {hft.price_to_float(ps.final_best_bid):.2f}  "
              f"Best ask: {hft.price_to_float(ps.final_best_ask):.2f}")

        # Access per-instrument order book.
        book = engine.order_book(ps.instrument_id)
        if book is not None:
            print(f"    Book spread: {book.spread_float():.4f}")

        # Access per-instrument analytics.
        inst_analytics = analytics.analytics(ps.instrument_id)
        if inst_analytics is not None:
            print(f"    Analytics: {inst_analytics.trade_count} trades, "
                  f"spread={inst_analytics.spread.avg_spread_bps():.2f} bps")

    # Print full analytics summary.
    print("\n=== Analytics Summary ===")
    analytics.print_summary()


if __name__ == "__main__":
    main()
