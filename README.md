# zplot

A small, dependency-free C++20 library that turns OHLC bars into **triangle vertices** and
computes **technical indicators** — oscillators, trend studies and volume / open-interest
studies. It draws nothing by itself: it produces geometry, and you hand that geometry to
whatever renderer you already have.

The bundled demo does exactly that with plain Win32 and an OpenGL 4.6 core profile —
no GLFW, no SDL, no Qt, no FreeType. The only third-party code anywhere is
[glad](https://glad.dav1d.de/) in the demo, vendored to load the GL entry points.

![zplot demo](docs/screenshot.png)

## Status

- **Charting + indicators**: stable, used in production by the author. 59 indicators,
  matched item-by-item against the reference formulas of Chinese trading terminals.
- **Demo**: complete and self-contained — builds and runs out of the box from a fresh clone.
- **CTP wrapper**: **not in this repository** (distributed separately — see
  [`CONTACT.md`](CONTACT.md)).

---

## Why it exists

Charting code usually arrives welded to a rendering framework. zplot is the part that is
actually about charts: bars in, triangles out.

- **Zero dependencies.** Standard library only. The library never links a graphics API.
- **Renderer-agnostic.** Output is a flat `std::vector` of `{x, y, r, g, b, a}` vertices laid
  out as triangles. Upload it to a vertex buffer and draw. The demo's whole render path is
  one shader and one `glDrawArrays` per panel.
- **Two-stage API.** `Compute*` returns *data-space* series plus the value range; geometry
  helpers map those to pixels and triangulate them. The range has to be known before a
  viewport can be configured, so the indicator is computed once and consumed twice.
- **Bars in, nothing out.** Input is a plain `std::vector<zplot::Bar>`. Build it from a CSV,
  a socket, or a live exchange callback — zplot performs no I/O for you.
- **Indicators that match a real terminal.** Definitions, periods and units are matched item
  by item against the reference formulas of the same name, including the unit traps
  (`DDI` / `SRDM` / `ADTM` return a `-1 ~ +1` ratio, not a percentage). See
  [`docs/indicators.md`](docs/indicators.md) for the full list and the match results.

## What is in the box

| Area | Contents |
|---|---|
| Candlesticks | candles (hollow up / solid down), bamboo (OHLC bars), close line, tower |
| Oscillators (24) | `MACD` `KDJ` `KD` `ROC` `RSI` `SLOWKD` `WR` `BIAS` `CR` `ATR` `DMI` `CCI` `PSY` `MTM` `DDI` `DMA` `ADTM` `ARBR` `LON` `SRDM` `SHORT` `MI` `DPO` `ASl` |
| Trend (21) | `MA` `EMA` `EMA2` `SMA` `TRMA` `TSMA` `BOLL` `BBlBOLL` `MA扩展` `PUBU` `SAR` `HCL` `MIKE` `BBI` `DKX` `CDP` `唐奇安` `ENV` `SP` `空` `SAR1` |
| Volume / OI (14) | `CJL` `MV` `CCL` `OPI` `OBV` `VR` `AD` `PVT` `WAD` `WVAD` `VOSC` `VROC` `VRSI` `价量运行趋势` |
| Geometry | viewport mapping, and triangulation of rectangles, thick segments, polylines, bars and scatter points |

## Repository layout

```
zplot/
├── include/                  the entire public API (8 headers)
│   ├── ZeBar.h               the input bar (OHLC + volume + open interest)
│   ├── ZePlotTypes.h         the vertex / text data contract
│   ├── color4f.h             RGBA color value type
│   ├── ZePlotGeometry.h      viewport mapping + triangulation helpers
│   ├── ZeKLineChart.h        candles, bamboo, close line, tower
│   ├── ZeOscillatorIndicator.h
│   ├── ZeTrendIndicator.h
│   └── ZeVolumeIndicator.h
├── src/                      charting + indicator sources (MIT, fully open)
│   ├── ZeKLineChart.cpp
│   ├── ZeOscillatorIndicator.cpp
│   ├── ZeTrendIndicator.cpp
│   └── ZeVolumeIndicator.cpp
├── demo/                     Win32 + OpenGL 4.6 viewer
│   ├── main.cpp              the whole application
│   ├── ZeOpenGL.h            WGL bootstrap: create a 4.6 core context
│   ├── ZeTextRenderer.h      GDI glyph atlas -> GL texture -> text quads
│   └── glad/                 vendored OpenGL loader (glad 0.1.36, gl=4.6 core)
│       ├── include/          glad.h + khrplatform.h
│       ├── src/glad.c
│       └── LICENSE
├── tools/
│   └── data/                 a sample 1-minute dataset
├── docs/
│   ├── screenshot.png
│   ├── index.md              documentation index / 文档总目录
│   ├── development.md        developer guide (EN) -- samples are compiled and run
│   ├── development.zh.md     开发者使用指南（中文）
│   ├── examples/             the guide's samples + verify-examples.ps1
│   ├── indicators.md         indicator formulas, params, and terminal-match results
│   └── indicators.zh.md      指标清单与文华 / 通达信对账（中文）
├── CONTACT.md                contact & contract work / 联系方式与定制开发
├── LICENSE                   MIT
└── CMakeLists.txt            builds the library and the demo
```

There are **no prebuilt binaries in this repository** — the demo compiles `src/*.cpp`
from source, so a fresh clone builds and runs without downloading anything.

## Quick start

```cpp
#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>

std::vector<zplot::Bar> bars = LoadYourBars();     // index must start at 0 and be gap-free

// 1. compute once -- data-space series plus the value range
zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
if (!macd.valid) { /* unknown name, too few parameters, or not enough bars */ }

// 2. configure a viewport from that range
zplot::Viewport vp;
vp.left = 0.0f;  vp.top = 0.0f;
vp.width = 800.0f; vp.height = 300.0f;
vp.xMin = 7600.0f; vp.xMax = 7999.0f;              // bar indices, shared with the candles
vp.yMin = macd.yMin; vp.yMax = macd.yMax;

// 3. triangles out
std::vector<zplot::PlotVertex> verts;
zplot::AppendSeries(verts, macd, vp);              // the indicator

// the candles, in their own viewport but sharing the indicator's x range
zplot::Viewport priceVp = zplot::FitViewport(bars, 0.0f, 0.0f, 800.0f, 500.0f, 400);
zplot::KLineStyle style;
zplot::AppendKLine(verts, bars, priceVp, style);

UploadToGpu(verts.data(), verts.size());           // 24 bytes per vertex, no index buffer
```

The default palettes are tuned for a **dark** canvas. On a light one, call
`macd.DimForLightBackground()` on each `SeriesPlot` once after computing — it darkens the
grey/white strokes and leaves saturated colors alone.

## Building the demo

```bash
cmake -B build
cmake --build build --config Release
```

The `zplot` static library and the `zplot_demo` executable are both built from source —
the demo compiles `src/*.cpp` and links nothing but the Win32 system libraries, so a
fresh clone builds and runs out of the box. On Windows with the Visual Studio toolset
installed, CMake picks the generator automatically; run `cmake --build build` from a
developer prompt (or pass `-G "Visual Studio 18 2026"` explicitly) if needed.

The demo takes an optional CSV path as its first argument, otherwise it looks for
`CZCE_MA701_1min.csv` next to the executable and then under `tools/data/`.

### Demo controls

| Input | Action |
|---|---|
| left-drag | pan the horizontal axis |
| left click | cycle the study shown by the panel you clicked |
| right click | reset the view |
| wheel | zoom around the cursor |
| `T` / `M` / `V` / `O` | next trend overlay / chart style / volume study / oscillator |
| `R` / `Esc` | reset the view / quit |

### Data format

`datetime,open,high,low,close,volume,open_oi,close_oi[,settle]`

The header row is skipped. Column 9 (settlement) is optional and falls back to the close,
so settlement-driven studies never degenerate into an empty chart.

## Requirements

- Windows, x64
- CMake 3.20+ and a C++20 compiler — developed and tested with MSVC on
  Visual Studio 2026 (toolset v145)
- An OpenGL 4.6 driver for the demo
- No third-party libraries at all; the demo vendors glad

## Third-party code

| Component | Where | License |
|---|---|---|
| glad 0.1.36 (OpenGL 4.6 core loader) | `demo/glad/` | MIT — see [`demo/glad/LICENSE`](demo/glad/LICENSE) |

## License

The charting and indicator code in `include/` and `src/` is **MIT** — see
[LICENSE](LICENSE).

---

# Documentation / 文档

- **[docs/index.md](docs/index.md)** — **文档总目录**，从这里进（中文）
- **[docs/development.zh.md](docs/development.zh.md)** — 开发者使用指南（中文）
- **[docs/development.md](docs/development.md)** — developer guide (EN),
  every sample compiled and run before publication
- **[docs/indicators.zh.md](docs/indicators.zh.md)** ·
  **[indicators.md](docs/indicators.md)** — 59 个指标的名字、公式、默认参数与对账结果
- **[CONTACT.md](CONTACT.md)** — contact & contract work / 联系方式与定制开发

zplot is MIT and the free part stays free forever. Issues and PRs are always
welcome.

