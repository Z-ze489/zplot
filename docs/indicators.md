# Indicator reference & cross-check results

> 中文版（文华 / 通达信对账）：[indicators.zh.md](indicators.zh.md)

zplot ships **59 indicators** (candlestick styles counted separately) in three
families. This document lists every name with its key default parameters, and
the cross-check results against the same-name indicators of **WenHua wh6** and
**TDX (通达信)** — the terminals used by Chinese futures traders, whose formulas
only exist in Chinese software.

> Indicators are dispatched by `name` string, e.g.
> `ComputeOscillator(bars, "MACD", {12,26,9})`. Too few parameters yields
> `valid == false`; nothing throws.

**Trademark note**: WenHua and TDX are named for identification purposes only
and are trademarks of their respective owners. zplot is an independent
implementation, not affiliated with, sponsored by, or endorsed by them. The
indicator formulas are public-domain mathematics.

## 1. Trend indicators (21)

| Name | Description | Defaults |
|---|---|---|
| `MA` | Simple moving average, one line per period given | 5 / 10 / 20 / 40 / 60 |
| `EMA` | Exponential moving average, same scheme | 5 / 10 / 20 / 40 / 60 |
| `EMA2` | Exponential family, up to 5 lines | 5 / 10 / 20 / 40 / 60 |
| `SMA` | Simple family, up to 8 lines | 5 / 10 / 20 / 40 / 60 |
| `TRMA` | Triple simple moving average (up to 5 lines) | — |
| `TSMA` | Triple exponential moving average (up to 5 lines) | — |
| `BOLL` | Bollinger bands: middle / upper / lower | 26 / 26 / 2 |
| `BBlBOLL` | Bollinger bands with BBI as the middle band | 10 / 3.0 |
| `MA扩展` | Two long-period moving averages (the name is the API string) | 120 / 240 |
| `PUBU` | Waterfall lines: six `(EMA(C,m)+MA(C,2m)+MA(C,4m))/3` | m = 4/6/9/13/18/24 |
| `SAR` / `SAR1` | Parabolic SAR, two dot series | start / cap / step |
| `HCL` | One MA each on high / close / low | 10 |
| `MIKE` | Six lines: WR / MR / SR / WS / MS / SS | 12 |
| `BBI` | Bull-bear index: four equal-weight SMAs combined | 3 / 6 / 12 / 24 |
| `DKX` | Bull-bear line: weighted mid price, then a MA of it | 10 |
| `CDP` | Counter-trend levels: CDP / NH / AH / NL / AL | none |
| `唐奇安` | Donchian channel (the name is the API string) | 20 / 20 |
| `ENV` | Envelope: SMA widened by a percentage both sides | 14 / 6.0 |
| `SP` | Settlement-price curve | params ignored |
| `空` | Draw nothing; `valid` still true (the name is the API string) | — |

## 2. Oscillator indicators (24)

| Name | Description | Defaults |
|---|---|---|
| `MACD` | DIF / DEA lines + histogram + zero axis | 12 / 26 / 9 |
| `KDJ` | Stochastic: K / D / J | 9 / 3 / 3 |
| `KD` | Stochastic, two-line version | 9 / 3 / 3 |
| `SLOWKD` | Slow stochastic | — |
| `RSI` | Relative strength index | 7 / 14 |
| `WR` | Williams %R | 14 |
| `ROC` | Rate of change | 12 / 6 |
| `BIAS` | Bias (multi-line) | 6 / 12 / 24 |
| `CCI` | Commodity channel index | 14 |
| `CR` | Energy index (multi-line) | 26 |
| `ARBR` | Popularity / willingness: AR, BR | 26 |
| `DMI` | PDI / MDI / ADX / ADXR | 14 / 6 |
| `ATR` | Average true range | 26 |
| `PSY` | Psychological line | — |
| `MTM` | Momentum | — |
| `DDI` | Directional deviation index (a ratio — see pitfalls) | 13 / 30 / 10 / 5 |
| `DMA` | Difference of two moving averages | — |
| `ADTM` | Dynamic buying/selling momentum (a ratio) | 23 / 8 |
| `LON` | Long-line study (volume-based) | 10 |
| `SHORT` | Short-line study (volume-based) | 5 |
| `SRDM` | (a ratio) | — |
| `MI` | Momentum index | — |
| `DPO` | Detrended price oscillator | — |
| `ASl` | Running total from the first bar (see pitfalls) | — |

## 3. Volume / open-interest indicators (14)

| Name | Description | Defaults |
|---|---|---|
| `CJL` | Volume bars + two volume MAs (the basic volume panel) | 5 / 10 |
| `MV` | Two recursively smoothed volume lines | — |
| `CCL` | Open-interest bars + an OI moving average | — |
| `OPI` | Open-interest **change** bars (build red / unwind green; this library's own) | — |
| `OBV` | On-balance volume | none |
| `VR` | Volume ratio | — |
| `AD` | Accumulation/distribution line (`((C-L)-(H-C))/(H-L)*VOL`, accumulated) | none |
| `PVT` | Price-volume trend (return-weighted, accumulated) | none |
| `WAD` | Williams accumulation/distribution | none |
| `WVAD` | Williams variable AD (`(C-O)/(H-L)*VOL`, **per bar, not accumulated**) | none |
| `VOSC` | Volume oscillator | short / long |
| `VROC` | Volume rate of change | N |
| `VRSI` | Volume RSI | N |
| `价量运行趋势` | One MA on price, one on volume, sync stretches shaded (the name is the API string) | 25 |

---

## 4. Cross-check results (WenHua wh6)

**Method**: the zplot algorithms are rewritten verbatim as WenHua formulas and
computed **by WenHua on its own data**, then compared with WenHua's built-in
indicators of the same name. A 1-tick difference in open/close prices between
data vendors is a **data factor**, not a formula error — so the comparison
checks whether the two lines coincide on the same data source.

**✅ Verified matching (exact or near-exact)**

| Indicator | Status |
|---|---|
| MACD | exact |
| ROC | exact |
| WR | exact |
| BIAS | exact |
| SRDM | exact |
| DMI (MDI line) | exact |
| DMA | near |
| DPO | near |
| MI | near |
| CR (first four lines) | near |

**⚠️ Formula verified; numeric offset comes from the data source**

For these, the numeric deviation is caused by the 1-tick open/close difference
between data vendors. The algorithm pasted into WenHua reproduces the built-in
lines exactly on the same data:

ADTM, DDI, RSI, CCI, ARBR, LON, SHORT

---

## 5. Unit pitfalls (the usual porting traps)

1. **Ratio vs percentage**: `DDI` / `SRDM` / `ADTM` return a **ratio** in
   `-1 ~ +1`, not a percentage; `ROC` / `RSI` / `PSY` / `CCI` / `WR` / `BIAS` /
   `CR` / `ARBR` really are percentages. Mixing these up is a 100x error.

2. **`ASl` accumulates from the first bar** — a different starting bar shifts
   every following value. When comparing against other software, load the same
   starting bar, or only the shape is comparable.

3. **Volume-based studies: compare shape, not magnitude** — if the volume unit
   differs (lots vs tonnes vs a constant factor), `LON` / `SHORT` scale by a
   constant; the curve shape is what must match.
