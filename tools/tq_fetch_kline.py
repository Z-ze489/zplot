#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
天勤（TqSdk）历史行情拉取 —— K 线
================================================================================

把期货合约的 K 线（含成交量、持仓量）拉到本地文件，用来验证指标算得准不准：
自己实现的 MA / MACD / DMI …… 跟行情软件对不上时，先看是不是数据源的问题。

依赖
    pip install tqsdk pandas

用法
    :: 首次使用先装依赖
        pip install tqsdk pandas

    :: 拉甲醇 2701 的 1 分钟线（默认 8000 根，交互式输入账号密码）
        python tq_fetch_kline.py

    :: 账号密码走命令行
        python tq_fetch_kline.py --user 你的天勤账号 --pass 你的密码

    :: 换合约 / 换周期（period 单位是秒：60=1分 300=5分 900=15分 3600=1时 86400=1日）
        python tq_fetch_kline.py --symbol CZCE.MA701 --period 60 --length 3000

    :: 不知道月份该写哪个？先列出该品种所有在市合约
        python tq_fetch_kline.py --list --symbol CZCE.MA

    :: 只做本地流程自检（不连天勤、不需要账号）
        python tq_fetch_kline.py --selftest

输出
    一行一根 K 线，列固定为：

        datetime, open, high, low, close, volume, open_oi, close_oi

    - datetime  北京时间（东八区）的 K 线**起点**时刻
    - open_oi   该 K 线起始时的持仓量
    - close_oi  该 K 线结束时的持仓量（持仓量类指标用这个）

    默认落在脚本同级的 data/ 目录，文件名自动带合约与周期。

账号
    三种给法，优先级从高到低：
      1. 命令行 --user / --pass
      2. 环境变量 TQ_USER / TQ_PASS
      3. 运行时交互输入（密码不回显）
    免费的天勤账号就能拉，单序列长度上限 8964 根。

合约代码
    天勤用「交易所.合约」写法，但**月份位数各交易所不一样**，这是最容易踩的坑：

        郑商所 CZCE   3 位 —— 甲醇 2027 年 1 月是 CZCE.MA701，不是 CZCE.MA2701
                            菜粕 CZCE.RM701 / 白糖 CZCE.SR701 / PTA CZCE.TA701
        上期所 SHFE   4 位 —— 螺纹 2027 年 1 月是 SHFE.rb2701
        大商所 DCE    4 位 —— 豆粕 2027 年 1 月是 DCE.m2701
        中金所 CFFEX  4 位 —— 沪深 300 是 CFFEX.IF2612

    郑商所误写成 4 位（CZCE.MA2701）时脚本会自动折成 CZCE.MA701，不用手动改。

    报 non-existent instrument 只有两种情况：
      1. 郑商所月份位数写错（上面的自动纠正会先接住）
      2. 这个月份确实没挂牌、或已经摘牌

    哪种都能先列一下该品种现在挂着哪些月份：

        python tq_fetch_kline.py --list --symbol CZCE.MA

    不想管月份就用量大的连续合约：主力 KQ.m@CZCE.MA / 指数 KQ.i@CZCE.MA。
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
import time

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

DEFAULT_SYMBOL = "CZCE.MA701"     # 甲醇 2701；郑商所月份是 3 位（7=2027 年，01=1 月）
DEFAULT_PERIOD = 60                # 秒；60 = 1 分钟
DEFAULT_LENGTH = 8000              # 常见取法，离上限 8964 留点余量
MAX_LENGTH = 8964                  # 天勤单序列硬上限

COLUMNS = ("datetime", "open", "high", "low", "close", "volume", "open_oi", "close_oi")

# 国内商品期货（含夜盘）大致覆盖的小时，用来粗查时区换算对不对
TRADING_HOURS = ((9, 11), (13, 15), (21, 23))


# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

def script_dir() -> str:
    """本脚本所在目录。"""
    return os.path.dirname(os.path.abspath(__file__))


def period_label(period: int) -> str:
    """把秒数说成人话。"""
    if period % 86400 == 0:
        return f"{period // 86400} 天"
    if period % 3600 == 0:
        return f"{period // 3600} 小时"
    if period % 60 == 0:
        return f"{period // 60} 分钟"
    return f"{period} 秒"


def period_tag(period: int) -> str:
    """秒数 -> 文件名里用的短标记。"""
    if period % 86400 == 0:
        return f"{period // 86400}d"
    if period % 3600 == 0:
        return f"{period // 3600}h"
    if period % 60 == 0:
        return f"{period // 60}min"
    return f"{period}s"


def fmt_num(value, decimals: int = 4) -> str:
    """
    数值 -> 写进 CSV 的文本。

    整数直接写成整数（2901.0 -> "2901"），小数去掉尾随零（0.3000 -> "0.3"），
    精度控制在 4 位小数以内。空值 / NaN 写成空串。
    """
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
    """数值 -> float 或 None（JSON 输出用）。"""
    try:
        f = float(value)
    except (TypeError, ValueError):
        return None
    return None if f != f else f


def default_out_path(symbol: str, period: int, fmt: str) -> str:
    """默认输出路径：脚本同级 data/<合约>_<周期>.<后缀>"""
    folder = os.path.join(script_dir(), "data")
    name = f"{symbol.replace('.', '_')}_{period_tag(period)}.{fmt}"
    return os.path.join(folder, name)


# ---------------------------------------------------------------------------
# 参数
# ---------------------------------------------------------------------------

def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="tq_fetch_kline.py",
        description="用天勤（TqSdk）拉期货合约的 K 线，存到本地供指标验证。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--symbol", default=DEFAULT_SYMBOL,
                        help=f"合约代码（默认 {DEFAULT_SYMBOL}）")
    parser.add_argument("--period", type=int, default=DEFAULT_PERIOD,
                        help=f"周期秒数，60=1 分钟（默认 {DEFAULT_PERIOD}）")
    parser.add_argument("--length", type=int, default=DEFAULT_LENGTH,
                        help=f"要多少根 K 线，上限 {MAX_LENGTH}（默认 {DEFAULT_LENGTH}）")
    parser.add_argument("--out", default=None,
                        help="输出文件路径（默认 data/<合约>_<周期>.<后缀>）")
    parser.add_argument("--format", choices=("csv", "json"), default="csv",
                        help="输出格式（默认 csv）")
    parser.add_argument("--encoding", default="utf-8-sig",
                        help="CSV 编码（默认 utf-8-sig，Excel 双击打开不乱码）")
    parser.add_argument("--user", default=None,
                        help="天勤账号（不给则读环境变量 TQ_USER，再不给就交互输入）")
    parser.add_argument("--pass", dest="password", default=None,
                        help="天勤密码（同上；建议用环境变量或交互输入，避免留在命令历史里）")
    parser.add_argument("--wait", type=float, default=30.0,
                        help="等待数据到齐的最长秒数（默认 30）")
    parser.add_argument("--drop-last", action="store_true",
                        help="丢掉最后一根（未收盘的那根），做历史回放时更干净")
    parser.add_argument("--quiet", action="store_true",
                        help="只打摘要，不打印头尾几行数据")
    parser.add_argument("--selftest", action="store_true",
                        help="不连天勤，用合成数据走一遍落盘流程")
    parser.add_argument("--list", dest="list_instruments", action="store_true",
                        help="只列出该品种的在市合约（配合 --symbol 用），不拉行情")
    return parser.parse_args(argv)


def resolve_auth(args: argparse.Namespace):
    """按 命令行 -> 环境变量 -> 交互输入 的顺序拿账号密码。"""
    user = args.user or os.environ.get("TQ_USER") or ""
    password = args.password or os.environ.get("TQ_PASS") or ""

    if not user:
        try:
            user = input("天勤账号：").strip()
        except EOFError:
            user = ""
    if not password:
        try:
            password = getpass.getpass("天勤密码（不回显）：")
        except EOFError:
            password = ""

    if not user or not password:
        raise SystemExit("账号或密码为空，退出。")
    return user, password


# ---------------------------------------------------------------------------
# 合约代码
# ---------------------------------------------------------------------------

def split_symbol(symbol: str):
    """CZCE.MA2701 -> ("CZCE", "MA2701")；没有交易所前缀就返回 ("", 原样)。"""
    if "@" in symbol:               # KQ.m@CZCE.MA 这类连续合约不拆
        return "", symbol
    ex, dot, code = symbol.partition(".")
    if not dot:
        return "", symbol
    return ex.upper(), code


def guess_product(code: str) -> str:
    """合约代码去掉月份 -> 品种：MA701 -> MA，rb2701 -> rb，IF2612 -> IF。"""
    i = len(code)
    while i > 0 and code[i - 1].isdigit():
        i -= 1
    return code[:i] if i else code


def normalize_symbol(symbol: str) -> str:
    """
    把合约代码收成天勤认的写法，返回改好的（没改就原样返回）。

    郑商所的月份是 3 位（MA605 = 2026 年 5 月），其它交易所是 4 位（rb2701）。
    写成 CZCE.MA2701 会被天勤判成 non-existent instrument —— 这里自动收成
    CZCE.MA701，省得每次都踩。
    """
    ex, code = split_symbol(symbol)
    if ex != "CZCE" or not code:
        return symbol
    i = len(code)
    while i > 0 and code[i - 1].isdigit():
        i -= 1
    digits = code[i:]
    if len(digits) == 4:            # 2701 -> 701
        return f"{ex}.{code[:i]}{digits[-3:]}"
    return symbol


def query_instruments(api, symbol: str):
    """
    查「和 symbol 同交易所同品种」的在市合约，返回排好序的代码列表。

    天勤的 query_quotes 要传品种代码，而大小写随交易所不同（郑商所 MA、
    上期所 rb），所以三种写法都试一遍；都查不到就拉回整个交易所本地过滤。
    """
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
    """订阅失败时给出能照抄的下一步：列候选，再给一条可直接跑的示例。"""
    ex, _ = split_symbol(symbol)
    product = guess_product(symbol.rpartition(".")[2])
    items = query_instruments(api, symbol)

    lines = []
    if items:
        lines.append(f"  该品种当前在市合约（{len(items)} 个）：")
        for i in range(0, len(items), 6):
            lines.append("    " + "  ".join(items[i:i + 6]))
        lines.append("  挑一个重跑：")
        lines.append(f"    python tq_fetch_kline.py --symbol {items[0]}")
        if ex:
            lines.append(f"  或者直接用连续合约：KQ.m@{ex}.{product}")
    else:
        lines.append("  没能查到候选合约，先列出该品种合约：")
        lines.append(f"    python tq_fetch_kline.py --list --symbol {symbol}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# 取数
# ---------------------------------------------------------------------------

def connect(user: str, password: str):
    """建 TqApi 连接，失败时直接退出并说明原因。"""
    try:
        from tqsdk import TqApi, TqAuth
    except ImportError:
        raise SystemExit("没装天勤库。先执行：pip install tqsdk pandas")

    try:
        return TqApi(auth=TqAuth(user, password))
    except Exception as exc:
        raise SystemExit(
            f"连接天勤失败。\n"
            f"  原始错误：{exc}\n"
            f"  检查账号密码；天勤免费账号可直接用，不需要额外开通。"
        )


def fetch_klines(symbol: str, period: int, length: int, wait_seconds: float,
                 user: str, password: str):
    """
    连天勤拉 K 线，返回 DataFrame 副本。

    天勤是「订阅 + 推送」模型：get_kline_serial 只是登记订阅，要 wait_update
    才会把数据填进 DataFrame。所以这里循环等，直到拿满 length 根、或者长度
    连续几次不再变化（说明就这么多），或超时。
    """
    api = connect(user, password)

    try:
        try:
            klines = api.get_kline_serial(symbol, period, data_length=length)
        except Exception as exc:
            raise SystemExit(
                f"订阅合约失败：{symbol}\n"
                f"  原始错误：{exc}\n"
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
                if stable >= 4:         # 连续 4 次长度不变，认定到齐
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
    """
    DataFrame -> [{列名: 值}] 列表。

    天勤的 datetime 是纳秒时间戳（UTC），加 8 小时得北京时间。
    开头的占位行（datetime <= 0）和坏行（open 为空）会被剔掉。
    """
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
        # 拿不到收盘持仓就退回起始持仓，字段不空缺
        "close_oi": column("close_oi") if "close_oi" in df.columns else column("open_oi"),
    }
    return [{key: data[key][i] for key in COLUMNS} for i in range(len(stamps))]


# ---------------------------------------------------------------------------
# 落盘
# ---------------------------------------------------------------------------

def write_records(records, path: str, fmt: str, encoding: str) -> None:
    """按指定格式写文件，目录不存在就建。"""
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
    """打一段摘要，顺便粗查时区换算。"""
    if not records:
        print("  没有拿到任何 K 线。")
        return

    highs = [r["high"] for r in records if r["high"] is not None]
    lows = [r["low"] for r in records if r["low"] is not None]
    vols = [r["volume"] for r in records if r["volume"] is not None]

    print(f"  根数    : {len(records)}")
    print(f"  起止    : {records[0]['datetime']}  ->  {records[-1]['datetime']}")
    if highs and lows:
        print(f"  价格区间: {min(lows):.4g} ~ {max(highs):.4g}")
    if vols:
        print(f"  成交量合: {sum(vols):.0f}")

    hours = sorted({int(r["datetime"][11:13]) for r in records})
    odd = [h for h in hours if not any(a <= h <= b for a, b in TRADING_HOURS)]
    if odd:
        print(f"  ** 警告：出现非交易时段的小时 {odd}，时区换算或合约选择可能有问题 **")
    else:
        print(f"  小时分布: {hours}  （都落在交易时段内，时区换算正常）")


def print_preview(records, head: int = 3, tail: int = 3) -> None:
    """打印头尾几行，方便肉眼核对。行数不足时直接全打，不重复。"""
    print("  列: " + ", ".join(COLUMNS))
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
# 列合约
# ---------------------------------------------------------------------------

def run_list(args: argparse.Namespace) -> int:
    """列出某品种的在市合约，不拉行情。--symbol 写 CZCE.MA 或 CZCE.MA701 都行。"""
    symbol = normalize_symbol(args.symbol)
    ex, code = split_symbol(symbol)
    product = guess_product(code)
    title = f"{ex}.{product}" if ex else product

    print("=" * 64)
    print("天勤合约查询")
    print(f"  品种  : {title}")
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
        print(f"没查到 {symbol} 对应的合约，确认交易所前缀和品种代码。")
        return 2

    print(f"当前在市合约（{len(items)} 个）：")
    for i in range(0, len(items), 6):
        print("  " + "  ".join(items[i:i + 6]))
    print()
    print("挑一个重跑：")
    print(f"  python tq_fetch_kline.py --symbol {items[0]}")
    return 0


# ---------------------------------------------------------------------------
# 自检
# ---------------------------------------------------------------------------

def run_selftest(args: argparse.Namespace) -> int:
    """不连天勤，用合成 K 线验证 时区转换 / 格式化 / 落盘 全流程。"""
    try:
        import pandas as pd
    except ImportError:
        print("自检需要 pandas：pip install pandas")
        return 1

    from datetime import datetime, timedelta, timezone

    print("自检模式：不连接天勤，用合成数据走一遍落盘流程。")

    # 北京时间 2026-09-29 21:00 起的 5 根 1 分钟线
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
        print("  自检失败：合成数据被过滤空了。")
        return 1

    out = args.out or os.path.join(script_dir(), "data", "_selftest.csv")
    write_records(records, out, args.format, args.encoding)
    print(f"  已写入 {out}")
    summarize(records)
    print_preview(records)

    ok = records[0]["datetime"] == "2026-09-29 21:00:00"
    print(f"  时区换算: {'正确' if ok else '错误'}"
          + ("" if ok else f"（头一行是 {records[0]['datetime']}，应为 2026-09-29 21:00:00）"))
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

def main(argv=None) -> int:
    args = parse_args(argv)

    if args.selftest:
        return run_selftest(args)

    if args.list_instruments:
        return run_list(args)

    symbol = normalize_symbol(args.symbol)
    if symbol != args.symbol:
        print(f"注意：郑商所合约月份是 3 位，{args.symbol} 已自动折成 {symbol}。")

    length = max(1, min(args.length, MAX_LENGTH))
    if length != args.length:
        print(f"注意：单序列上限 {MAX_LENGTH} 根，已从 {args.length} 收到 {length}。")

    out_path = args.out or default_out_path(symbol, args.period, args.format)

    print("=" * 64)
    print("天勤 K 线拉取")
    print(f"  合约  : {symbol}")
    print(f"  周期  : {period_label(args.period)}（{args.period} 秒）")
    print(f"  长度  : {length} 根")
    print(f"  输出  : {out_path}")
    print("=" * 64)

    user, password = resolve_auth(args)

    print("正在连接天勤...")
    df = fetch_klines(symbol, args.period, length, args.wait, user, password)
    print(f"收到 {len(df)} 行原始数据。")

    records = build_records(df, drop_last=args.drop_last)
    if not records:
        print("没有可用数据。合约是否已到期？或该周期数据为空？")
        return 2

    write_records(records, out_path, args.format, args.encoding)
    print(f"已写入 {out_path}")
    summarize(records)
    if not args.quiet:
        print_preview(records)
    return 0


if __name__ == "__main__":
    sys.exit(main())
