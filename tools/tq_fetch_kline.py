#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fetch historical futures klines via TqSdk (TianQin).

    pip install tqsdk pandas
    python tq_fetch_kline.py --symbol CZCE.MA701 --period 60 --length 8000
    python tq_fetch_kline.py --list --symbol CZCE.MA      # list listed contracts
    python tq_fetch_kline.py --selftest                   # no account needed

Output, one bar per line:

    datetime, open, high, low, close, volume, open_oi, close_oi

datetime is the bar's start time in Beijing time (UTC+8); close_oi is the open
interest at the end of the bar. Files land in data/ next to the script.

Credentials: --user/--pass, or env TQ_USER/TQ_PASS, or interactive input.
A free TianQin account works; the per-series cap is 8964 bars.

CZCE contract months are 3 digits (CZCE.MA701, not MA2701); other exchanges
use 4 (SHFE.rb2701). A 4-digit CZCE month is auto-folded by this script.
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
import time

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DEFAULT_SYMBOL = "CZCE.MA701"
DEFAULT_PERIOD = 60                # seconds; 60 = 1 minute
DEFAULT_LENGTH = 8000
MAX_LENGTH = 8964                  # TianQin per-series hard cap

COLUMNS = ("datetime", "open", "high", "low", "close", "volume", "open_oi", "close_oi")

# Hours roughly covered by Chinese commodity futures (incl. night sessions),
# used as a sanity check on the timezone conversion.
TRADING_HOURS = ((9, 11), (13, 15), (21, 23))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def script_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def period_label(period: int) -> str:
    if period % 86400 == 0:
        return f"{period // 86400} day"
    if period % 3600 == 0:
        return f"{period // 3600} hour"
    if period % 60 == 0:
        return f"{period // 60} minute"
    return f"{period} second"


def period_tag(period: int) -> str:
    if period % 86400 == 0:
        return f"{period // 86400}d"
    if period % 3600 == 0:
        return f"{period // 3600}h"
    if period % 60 == 0:
        return f"{period // 60}min"
    return f"{period}s"


def fmt_num(value, decimals: int = 4) -> str:
    """Number -> CSV text: integral values as integers, trailing zeros trimmed, NaN as empty."""
    if value is None:
        return ""
    try:
        f = float(value)
    except (TypeError, ValueError):
        return ""
    if f != f:                      # NaN
        return ""
    text = f"{f:.{decimals}f}"
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text if text not in ("", "-") else "0"


def num_or_none(value):
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return None if f != f else f


def default_out_path(symbol: str, period: int, fmt: str) -> str:
    folder = os.path.join(script_dir(), "data")
    name = f"{symbol.replace('.', '_')}_{period_tag(period)}.{fmt}"
    return os.path.join(folder, name)


# ---------------------------------------------------------------------------
# Arguments
# ---------------------------------------------------------------------------

def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="tq_fetch_kline.py",
        description="Fetch futures klines via TqSdk (TianQin) and save them locally for indicator verification.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--symbol", default=DEFAULT_SYMBOL,
                        help=f"contract code (default {DEFAULT_SYMBOL})")
    parser.add_argument("--period", type=int, default=DEFAULT_PERIOD,
                        help=f"period in seconds, 60 = 1 minute (default {DEFAULT_PERIOD})")
    parser.add_argument("--length", type=int, default=DEFAULT_LENGTH,
                        help=f"number of bars to fetch, cap {MAX_LENGTH} (default {DEFAULT_LENGTH})")
    parser.add_argument("--out", default=None,
                        help="output file path (default data/<contract>_<period>.<ext>)")
    parser.add_argument("--format", choices=("csv", "json"), default="csv",
                        help="output format (default csv)")
    parser.add_argument("--encoding", default="utf-8-sig",
                        help="CSV encoding (default utf-8-sig, opens cleanly in Excel)")
    parser.add_argument("--user", default=None,
                        help="TianQin account (falls back to env TQ_USER, then interactive input)")
    parser.add_argument("--pass", dest="password", default=None,
                        help="TianQin password (same fallbacks)")
    parser.add_argument("--wait", type=float, default=30.0,
                        help="max seconds to wait for data to settle (default 30)")
    parser.add_argument("--drop-last", action="store_true",
                        help="drop the last (still-forming) bar")
    parser.add_argument("--quiet", action="store_true",
                        help="print only the summary, no head/tail preview rows")
    parser.add_argument("--selftest", action="store_true",
                        help="run the write pipeline on synthetic data, no connection")
    parser.add_argument("--list", dest="list_instruments", action="store_true",
                        help="only list currently listed contracts (use with --symbol)")
    return parser.parse_args(argv)


def resolve_auth(args: argparse.Namespace):
    user = args.user or os.environ.get("TQ_USER") or ""
    password = args.password or os.environ.get("TQ_PASS") or ""

    if not user:
        try:
            user = input("TianQin account: ").strip()
        except EOFError:
            user = ""
    if not password:
        try:
            password = getpass.getpass("TianQin password (not echoed): ")
        except EOFError:
            password = ""

    if not user or not password:
        raise SystemExit("Empty account or password, exiting.")
    return user, password


# ---------------------------------------------------------------------------
# Contract codes
# ---------------------------------------------------------------------------

def split_symbol(symbol: str):
    """CZCE.MA2701 -> ("CZCE", "MA2701"); no exchange prefix -> ("", as-is)."""
    if "@" in symbol:               # continuous contracts like KQ.m@CZCE.MA are not split
        return "", symbol
    ex, dot, code = symbol.partition(".")
    if not dot:
        return "", symbol
    return ex.upper(), code


def guess_product(code: str) -> str:
    """MA701 -> MA, rb2701 -> rb, IF2612 -> IF."""
    i = len(code)
    while i > 0 and code[i - 1].isdigit():
        i -= 1
    return code[:i] if i else code


def normalize_symbol(symbol: str) -> str:
    """Fold a 4-digit CZCE month (CZCE.MA2701) into 3 digits (CZCE.MA701)."""
    ex, code = split_symbol(symbol)
    if ex != "CZCE" or not code:
        return symbol
    i = len(code)
    while i > 0 and code[i - 1].isdigit():
        i -= 1
    digits = code[i:]
    if len(digits) == 4:
        return f"{ex}.{code[:i]}{digits[-3:]}"
    return symbol


def query_instruments(api, symbol: str):
    """Listed contracts matching the same exchange and product; sorted."""
    ex, code = split_symbol(symbol)
    if not ex:
        return []
    product = guess_product(code)

    for prod in (product, product.lower(), product.upper()):
        try:
            res = api.query_quotes(ins_class="FUTURE", exchange_id=ex,
                                   product_id=prod, expired=False)
        except Exception:
            res = None
        if res:
            return sorted(res)

    try:
        res = api.query_quotes(ins_class="FUTURE", exchange_id=ex, expired=False)
    except Exception:
        return []
    if not res:
        return []
    prefix = f"{ex}."
    hit = [s for s in res
           if s.startswith(prefix)
           and guess_product(s[len(prefix):]).upper() == product.upper()]
    return sorted(hit)


def format_instrument_hint(api, symbol: str) -> str:
    """On subscription failure, show candidate contracts and a ready-to-run command."""
    ex, _ = split_symbol(symbol)
    product = guess_product(symbol.rpartition(".")[2])
    items = query_instruments(api, symbol)

    lines = []
    if items:
        lines.append(f"  Currently listed contracts for this product ({len(items)}):")
        for i in range(0, len(items), 6):
            lines.append("    " + "  ".join(items[i:i + 6]))
        lines.append("  Pick one and rerun:")
        lines.append(f"    python tq_fetch_kline.py --symbol {items[0]}")
        if ex:
            lines.append(f"  Or use the continuous contract directly: KQ.m@{ex}.{product}")
    else:
        lines.append("  Could not query candidate contracts; list the product's contracts first:")
        lines.append(f"    python tq_fetch_kline.py --list --symbol {symbol}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Fetch
# ---------------------------------------------------------------------------

def connect(user: str, password: str):
    try:
        from tqsdk import TqApi, TqAuth
    except ImportError:
        raise SystemExit("The tqsdk package is missing. Run: pip install tqsdk pandas")

    try:
        return TqApi(auth=TqAuth(user, password))
    except Exception as exc:
        raise SystemExit(
            f"Failed to connect to TianQin.\n"
            f"  Original error: {exc}\n"
            f"  Check the account and password; a free TianQin account works out of the box."
        )


def fetch_klines(symbol: str, period: int, length: int, wait_seconds: float,
                 user: str, password: str):
    """
    TianQin is subscribe-and-push: get_kline_serial only registers the
    subscription, wait_update fills the DataFrame. Loop until `length` bars
    arrived, the length stabilized, or the timeout hit.
    """
    api = connect(user, password)

    try:
        try:
            klines = api.get_kline_serial(symbol, period, data_length=length)
        except Exception as exc:
            raise SystemExit(
                f"Failed to subscribe to contract: {symbol}\n"
                f"  Original error: {exc}\n"
                + format_instrument_hint(api, symbol)
            )

        deadline = time.time() + wait_seconds
        last_n, stable = -1, 0
        while time.time() < deadline:
            api.wait_update(deadline=time.time() + 0.5)
            n = len(klines)
            if n >= length:
                break
            if n == last_n:
                stable += 1
                if stable >= 4:
                    break
            else:
                last_n, stable = n, 0

        return klines.copy()
    finally:
        try:
            api.close()
        except Exception:
            pass


def build_records(df, drop_last: bool = False):
    """DataFrame -> list of {column: value}; TianQin datetime is ns UTC, +8h = Beijing time."""
    import pandas as pd

    df = df[df["datetime"] > 0]
    if "open" in df.columns:
        df = df[df["open"].notna()]
    if drop_last and len(df) > 1:
        df = df.iloc[:-1]
    if len(df) == 0:
        return []

    dt_bj = pd.to_datetime(df["datetime"].astype("int64"), unit="ns") + pd.Timedelta(hours=8)
    stamps = dt_bj.dt.strftime("%Y-%m-%d %H:%M:%S").tolist()

    def column(name):
        if name in df.columns:
            return df[name].tolist()
        return [None] * len(df)

    data = {
        "datetime": stamps,
        "open": column("open"),
        "high": column("high"),
        "low": column("low"),
        "close": column("close"),
        "volume": column("volume"),
        "open_oi": column("open_oi"),
        "close_oi": column("close_oi") if "close_oi" in df.columns else column("open_oi"),
    }
    return [{key: data[key][i] for key in COLUMNS} for i in range(len(stamps))]


# ---------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------

def write_records(records, path: str, fmt: str, encoding: str) -> None:
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)

    if fmt == "json":
        with open(path, "w", encoding="utf-8", newline="\n") as fp:
            fp.write("[\n")
            for i, row in enumerate(records):
                item = {key: (row[key] if key == "datetime" else num_or_none(row[key]))
                        for key in COLUMNS}
                fp.write("  " + json.dumps(item, ensure_ascii=False))
                fp.write(",\n" if i + 1 < len(records) else "\n")
            fp.write("]\n")
    else:
        with open(path, "w", encoding=encoding, newline="\n") as fp:
            fp.write(",".join(COLUMNS) + "\n")
            for row in records:
                fp.write(row["datetime"] + ","
                         + ",".join(fmt_num(row[key]) for key in COLUMNS[1:]) + "\n")


def summarize(records) -> None:
    if not records:
        print("  No klines were fetched.")
        return

    highs = [r["high"] for r in records if r["high"] is not None]
    lows = [r["low"] for r in records if r["low"] is not None]
    vols = [r["volume"] for r in records if r["volume"] is not None]

    print(f"  bars        : {len(records)}")
    print(f"  range       : {records[0]['datetime']}  ->  {records[-1]['datetime']}")
    if highs and lows:
        print(f"  price span  : {min(lows):.4g} ~ {max(highs):.4g}")
    if vols:
        print(f"  volume total: {sum(vols):.0f}")

    hours = sorted({int(r["datetime"][11:13]) for r in records})
    odd = [h for h in hours if not any(a <= h <= b for a, b in TRADING_HOURS)]
    if odd:
        print(f"  ** WARNING: hours outside trading sessions detected {odd}; "
              f"the timezone conversion or the contract choice may be wrong **")
    else:
        print(f"  hour spread : {hours}  (all inside trading sessions, timezone OK)")


def print_preview(records, head: int = 3, tail: int = 3) -> None:
    print("  columns: " + ", ".join(COLUMNS))
    if len(records) <= head + tail:
        head_rows, tail_rows = records, []
    else:
        head_rows, tail_rows = records[:head], records[-tail:]

    def show(row):
        print("      " + row["datetime"] + ","
              + ",".join(fmt_num(row[key]) for key in COLUMNS[1:]))

    for row in head_rows:
        show(row)
    if tail_rows:
        print("      ...")
        for row in tail_rows:
            show(row)


# ---------------------------------------------------------------------------
# List contracts
# ---------------------------------------------------------------------------

def run_list(args: argparse.Namespace) -> int:
    symbol = normalize_symbol(args.symbol)
    ex, code = split_symbol(symbol)
    product = guess_product(code)
    title = f"{ex}.{product}" if ex else product

    print("=" * 64)
    print("TianQin contract lookup")
    print(f"  product : {title}")
    print("=" * 64)

    user, password = resolve_auth(args)
    api = connect(user, password)
    try:
        items = query_instruments(api, symbol)
    finally:
        try:
            api.close()
        except Exception:
            pass

    if not items:
        print(f"No contracts matched {symbol}; check the exchange prefix and the product code.")
        return 2

    print(f"Currently listed contracts ({len(items)}):")
    for i in range(0, len(items), 6):
        print("  " + "  ".join(items[i:i + 6]))
    print()
    print("Pick one and rerun:")
    print(f"  python tq_fetch_kline.py --symbol {items[0]}")
    return 0


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

def run_selftest(args: argparse.Namespace) -> int:
    try:
        import pandas as pd
    except ImportError:
        print("Self-test needs pandas: pip install pandas")
        return 1

    from datetime import datetime, timedelta, timezone

    print("Self-test mode: running the write pipeline on synthetic data, no connection.")

    # 5 one-minute bars starting at 2026-09-29 21:00 Beijing time
    first_bj = datetime(2026, 9, 29, 21, 0, 0, tzinfo=timezone(timedelta(hours=8)))
    base_ns = int(first_bj.timestamp() * 1e9)

    rows = []
    for i in range(5):
        rows.append({
            "datetime": base_ns + i * 60 * 1_000_000_000,
            "open": 2900.0 + i,
            "high": 2903.0 + i,
            "low": 2898.0 + i,
            "close": 2901.5 + i,
            "volume": 100.0 + i,
            "open_oi": 50000.0 + i * 10,
            "close_oi": 50005.0 + i * 10,
        })
    df = pd.DataFrame(rows)

    records = build_records(df)
    if not records:
        print("  Self-test failed: the synthetic data was filtered away entirely.")
        return 1

    out = args.out or os.path.join(script_dir(), "data", "_selftest.csv")
    write_records(records, out, args.format, args.encoding)
    print(f"  Written to {out}")
    summarize(records)
    print_preview(records)

    ok = records[0]["datetime"] == "2026-09-29 21:00:00"
    print(f"  timezone conversion: {'OK' if ok else 'WRONG'}"
          + ("" if ok else f" (first row is {records[0]['datetime']}, expected 2026-09-29 21:00:00)"))
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv=None) -> int:
    args = parse_args(argv)

    if args.selftest:
        return run_selftest(args)

    if args.list_instruments:
        return run_list(args)

    symbol = normalize_symbol(args.symbol)
    if symbol != args.symbol:
        print(f"Note: CZCE contract months are 3 digits; {args.symbol} was folded to {symbol}.")

    length = max(1, min(args.length, MAX_LENGTH))
    if length != args.length:
        print(f"Note: per-series cap is {MAX_LENGTH} bars; trimmed {args.length} -> {length}.")

    out_path = args.out or default_out_path(symbol, args.period, args.format)

    print("=" * 64)
    print("TianQin kline fetch")
    print(f"  contract : {symbol}")
    print(f"  period   : {period_label(args.period)} ({args.period} s)")
    print(f"  length   : {length} bars")
    print(f"  output   : {out_path}")
    print("=" * 64)

    user, password = resolve_auth(args)

    print("Connecting to TianQin...")
    df = fetch_klines(symbol, args.period, length, args.wait, user, password)
    print(f"Received {len(df)} raw rows.")

    records = build_records(df, drop_last=args.drop_last)
    if not records:
        print("No usable data. Has the contract expired, or is this period empty?")
        return 2

    write_records(records, out_path, args.format, args.encoding)
    print(f"Written to {out_path}")
    summarize(records)
    if not args.quiet:
        print_preview(records)
    return 0


if __name__ == "__main__":
    sys.exit(main())
