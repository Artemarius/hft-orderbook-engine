"""Simple single-instrument replay example.

Replays BTC/USDT L3 order data through the matching engine and prints
execution statistics and final order book state.

Usage:
    python python/examples/simple_replay.py
"""

import os
import sys

# Allow running from the repo root.
script_dir = os.path.dirname(os.path.abspath(__file__))
python_dir = os.path.dirname(script_dir)
sys.path.insert(0, python_dir)

import hft_orderbook as hft


def main():
    # Locate sample data relative to this script.
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

    # Create engine and run.
    engine = hft.ReplayEngine(config)
    stats = engine.run()

    # Print stats.
    print("=== Replay Statistics ===")
    print(f"  Total messages:     {stats.total_messages}")
    print(f"  Orders accepted:    {stats.orders_accepted}")
    print(f"  Orders cancelled:   {stats.orders_cancelled}")
    print(f"  Trades generated:   {stats.trades_generated}")
    print(f"  Elapsed:            {stats.elapsed_seconds:.3f}s")
    print(f"  Throughput:         {stats.messages_per_second:,.0f} msg/s")

    # Inspect final book state.
    book = engine.order_book
    print(f"\n=== Final Order Book ===")
    print(f"  Orders on book:     {book.order_count}")
    print(f"  Spread:             {book.spread_float():.4f}")
    print(f"  Mid price:          {book.mid_price_float():.2f}")

    bid = book.best_bid()
    ask = book.best_ask()
    if bid:
        print(f"  Best bid:           {bid['price_float']:.2f} x {bid['quantity']}")
    if ask:
        print(f"  Best ask:           {ask['price_float']:.2f} x {ask['quantity']}")

    # Show top-5 depth.
    print("\n  Bid depth (top 5):")
    for level in book.get_bid_depth(5):
        print(f"    {level['price_float']:.2f}  qty={level['quantity']}  orders={level['order_count']}")

    print("  Ask depth (top 5):")
    for level in book.get_ask_depth(5):
        print(f"    {level['price_float']:.2f}  qty={level['quantity']}  orders={level['order_count']}")

    # Price conversion helpers.
    print(f"\n=== Price Helpers ===")
    print(f"  PRICE_SCALE = {hft.PRICE_SCALE}")
    print(f"  float_to_price(42000.50) = {hft.float_to_price(42000.50)}")
    print(f"  price_to_float(4200050000000) = {hft.price_to_float(4200050000000)}")


if __name__ == "__main__":
    main()
