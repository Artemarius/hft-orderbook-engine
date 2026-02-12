#!/usr/bin/env python3
import random
import csv
import os

OUTPUT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "btcusdt_l3_sample.csv")
START_TS = 1704067200000000000
TS_INCREMENT = 100_000
SEED = 42
random.seed(SEED)

next_order_id = 1
current_ts = START_TS
rows = []
active_orders = {}
buy_orders = set()
sell_orders = set()

def alloc_order_id():
    global next_order_id
    oid = next_order_id
    next_order_id += 1
    return oid

def tick():
    global current_ts
    ts = current_ts
    current_ts += TS_INCREMENT
    return ts

def fmt_price(p):
    return "{:.2f}".format(p)

def add_order(side, price, quantity):
    oid = alloc_order_id()
    ts = tick()
    rows.append((ts, "ADD", oid, side, fmt_price(price), quantity))
    active_orders[oid] = {"side": side, "price": price, "quantity": quantity}
    if side == "BUY":
        buy_orders.add(oid)
    else:
        sell_orders.add(oid)
    return oid

def cancel_order(oid):
    ts = tick()
    rows.append((ts, "CANCEL", oid, "", fmt_price(active_orders[oid]["price"]),
                 active_orders[oid]["quantity"]))
    del active_orders[oid]
    buy_orders.discard(oid)
    sell_orders.discard(oid)

def emit_trade(side, price, quantity):
    ts = tick()
    rows.append((ts, "TRADE", "", side, fmt_price(price), quantity))

def phase1():
    target = 500
    count = 0
    buy_levels = [round(41950.0 + i * 0.5, 2) for i in range(101)]
    for lvl in buy_levels:
        n = random.randint(2, 5)
        for _ in range(n):
            if count >= target:
                return
            qty = random.randint(1, 20)
            add_order("BUY", lvl, qty)
            count += 1
    sell_levels = [round(42001.0 + i * 0.5, 2) for i in range(99)]
    for lvl in sell_levels:
        n = random.randint(2, 5)
        for _ in range(n):
            if count >= target:
                return
            qty = random.randint(1, 20)
            add_order("SELL", lvl, qty)
            count += 1
    while count < target:
        if random.random() < 0.5:
            lvl = random.choice(buy_levels)
            add_order("BUY", lvl, random.randint(1, 20))
        else:
            lvl = random.choice(sell_levels)
            add_order("SELL", lvl, random.randint(1, 20))
        count += 1

def phase2():
    target = 2000
    count = 0
    price_min = 41950.0
    price_max = 42050.0
    while count < target:
        r = random.random()
        if r < 0.60:
            side = "BUY" if random.random() < 0.5 else "SELL"
            if side == "BUY":
                if random.random() < 0.15:
                    price = round(random.uniform(42001.0, 42010.0) * 2) / 2
                else:
                    price = round(random.uniform(price_min, 42000.0) * 2) / 2
            else:
                if random.random() < 0.15:
                    price = round(random.uniform(41990.0, 42000.0) * 2) / 2
                else:
                    price = round(random.uniform(42001.0, price_max) * 2) / 2
            qty = random.randint(1, 20)
            add_order(side, price, qty)
            count += 1
        elif r < 0.90:
            if not active_orders:
                continue
            oid = random.choice(list(active_orders.keys()))
            cancel_order(oid)
            count += 1
        else:
            side = "BUY" if random.random() < 0.5 else "SELL"
            price = round(random.uniform(42000.0, 42001.0) * 2) / 2
            qty = random.randint(1, 10)
            emit_trade(side, price, qty)
            count += 1

def phase3():
    target = 1000
    count = 0
    mid_start = 42000.0
    mid_end = 42100.0
    while count < target:
        progress = count / target
        mid = mid_start + (mid_end - mid_start) * progress
        r = random.random()
        if r < 0.55:
            side = "BUY" if random.random() < 0.55 else "SELL"
            if side == "BUY":
                if random.random() < 0.35:
                    price = round(random.uniform(mid, mid + 15.0) * 2) / 2
                else:
                    price = round(random.uniform(mid - 50.0, mid) * 2) / 2
            else:
                if random.random() < 0.25:
                    price = round(random.uniform(mid - 10.0, mid) * 2) / 2
                else:
                    price = round(random.uniform(mid, mid + 50.0) * 2) / 2
            price = max(41000.0, min(43000.0, price))
            qty = random.randint(1, 50)
            add_order(side, price, qty)
            count += 1
        elif r < 0.80:
            if not active_orders:
                continue
            oid = random.choice(list(active_orders.keys()))
            cancel_order(oid)
            count += 1
        else:
            side = "BUY" if random.random() < 0.55 else "SELL"
            price = round(random.uniform(mid - 2.0, mid + 2.0) * 2) / 2
            price = max(41000.0, min(43000.0, price))
            qty = random.randint(1, 30)
            emit_trade(side, price, qty)
            count += 1

def phase4():
    target = 1500
    count = 0
    mid = 42050.0
    while count < target:
        r = random.random()
        if r < 0.65:
            side = "BUY" if random.random() < 0.5 else "SELL"
            if side == "BUY":
                price = round(random.uniform(mid - 50.0, mid) * 2) / 2
                if random.random() < 0.08:
                    price = round(random.uniform(mid, mid + 5.0) * 2) / 2
            else:
                price = round(random.uniform(mid + 0.5, mid + 50.0) * 2) / 2
                if random.random() < 0.08:
                    price = round(random.uniform(mid - 5.0, mid) * 2) / 2
            price = max(41000.0, min(43000.0, price))
            qty = random.randint(1, 20)
            add_order(side, price, qty)
            count += 1
        elif r < 0.90:
            if not active_orders:
                continue
            oid = random.choice(list(active_orders.keys()))
            cancel_order(oid)
            count += 1
        else:
            side = "BUY" if random.random() < 0.5 else "SELL"
            price = round(random.uniform(mid - 1.0, mid + 1.0) * 2) / 2
            price = max(41000.0, min(43000.0, price))
            qty = random.randint(1, 10)
            emit_trade(side, price, qty)
            count += 1

def main():
    print("Generating L3 data...")
    phase1()
    print("  Phase 1: {} rows (active={}, buys={}, sells={})".format(
        len(rows), len(active_orders), len(buy_orders), len(sell_orders)))
    phase2()
    print("  Phase 2: {} rows (active={}, buys={}, sells={})".format(
        len(rows), len(active_orders), len(buy_orders), len(sell_orders)))
    phase3()
    print("  Phase 3: {} rows (active={}, buys={}, sells={})".format(
        len(rows), len(active_orders), len(buy_orders), len(sell_orders)))
    phase4()
    print("  Phase 4: {} rows (active={}, buys={}, sells={})".format(
        len(rows), len(active_orders), len(buy_orders), len(sell_orders)))
    total = len(rows)
    print("Total rows: {}".format(total))
    assert 4900 <= total <= 5100
    fb = len(buy_orders)
    fs = len(sell_orders)
    print("Final active -- BUY: {}, SELL: {}".format(fb, fs))
    assert fb > 0 and fs > 0
    for row in rows:
        p = float(row[4])
        assert 41000.0 <= p <= 43000.0
    cids = set()
    for row in rows:
        if row[1] == "CANCEL":
            assert row[2] not in cids
            cids.add(row[2])
    aids = set()
    for row in rows:
        if row[1] == "ADD":
            aids.add(row[2])
        elif row[1] == "CANCEL":
            assert row[2] in aids
    na = sum(1 for r in rows if r[1] == "ADD")
    nc = sum(1 for r in rows if r[1] == "CANCEL")
    nt = sum(1 for r in rows if r[1] == "TRADE")
    print("Events -- ADD: {}, CANCEL: {}, TRADE: {}".format(na, nc, nt))
    with open(OUTPUT_PATH, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["timestamp","event_type","order_id","side","price","quantity"])
        for row in rows:
            w.writerow(row)
    print("Wrote {} rows to {}".format(total, OUTPUT_PATH))

if __name__ == "__main__":
    main()
