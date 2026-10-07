# Developer guide

> 中文版：[development.zh.md](development.zh.md) ·
> 文档总目录：[index.md](index.md)

How to put zplot into an application. Covers the whole path: building the  
library, feeding it bars, computing indicators, turning them into vertices, and  
handing those vertices to a renderer of your choice.

Every code sample in this document is a real file under  
[`docs/examples/`](examples/) that is compiled **and run** before it is quoted  
here. The output shown in each section is copied from an actual run, not  
written by hand.

**Contents**

1. [What you get, and what you don't](#1-what-you-get-and-what-you-dont)
2. [Build](#2-build)
3. [The data contract](#3-the-data-contract)
4. [Two stages: compute, then draw](#4-two-stages-compute-then-draw)
5. [Indicators](#5-indicators)
6. [Geometry](#6-geometry)
7. [Colors, ranges and fitting](#7-colors-ranges-and-fitting)
8. [Wiring the vertices into your renderer](#8-wiring-the-vertices-into-your-renderer)
9. [Verifying the samples yourself](#9-verifying-the-samples-yourself)
10. [Common mistakes](#10-common-mistakes)

---

## 1. What you get, and what you don't

|                    |                                                                                                              |
| ------------------ | ------------------------------------------------------------------------------------------------------------ |
| **In the box**     | Candlestick geometry (5 styles), 59 indicators, viewport mapping, triangulation helpers (`include/`, `src/`) |
| **Not in the box** | Any renderer, any window, any font, any file/socket I/O                                                      |
| **You supply**     | A `std::vector<Bar>`, a place to draw, and a way to close the window                                         |

zplot never touches a graphics API and never opens a file. It is a pure  
function library: data in, triangles out. That is why it links nothing but the  
C++ standard library.

## 2. Build

```bash
cmake -B build
cmake --build build --config Release
```

Requirements: Windows x64, CMake 3.20+, a C++20 compiler (developed on MSVC  
toolset v145). The library itself has **no** third-party dependency; the demo  
vendors `glad` to load GL entry points.

To use only the library in your own project, add the sources directly — there  
is nothing to link:

```cmake
add_subdirectory(zplot)
target_link_libraries(your_app PRIVATE zplot)
```

## 3. The data contract

Everything starts with `std::vector<zplot::Bar>`  
([`include/ZeBar.h`](../include/ZeBar.h)):

```cpp
struct Bar {
    int         index = 0;          // horizontal plot coordinate, 0-based
    std::string time;               // time label
    double      open, high, low, close;
    double      volume;             // lots
    double      openInterest;       // lots
    double      settle;             // falls back to close when 0
};
```

Two rules matter:

- **`index` must start at 0 and be gap-free.** The horizontal axis is built  
  from `index`, not from the array position. A gap in `index` becomes a gap in  
  the chart; a non-zero start shifts every panel.
- **`index` must be ascending.** Ordering is never checked for you.

## 4. Two stages: compute, then draw

Every indicator follows the same shape, and understanding why prevents most  
mistakes. The value range has to be known *before* a viewport can be  
configured, so an indicator is computed once and consumed twice.

```
Compute*()           ->  SeriesPlot   data-space points + the value range
Viewport             <-  built from that range
Append*/AppendSeries ->  PlotVertex[] pixels, as triangles
```

### Quick start

The sample is [`examples/01_quickstart.cpp`](examples/01_quickstart.cpp).

```cpp
#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>

std::vector<zplot::Bar> bars = LoadYourBars();   // index must start at 0

// 1. compute once -- data-space series plus the value range
zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
if (!macd.valid) { /* unknown name, too few parameters, or not enough bars */ }

// 2. configure a viewport from that range
zplot::Viewport vp;
vp.left = 0.0f;   vp.top = 0.0f;
vp.width = 800.0f; vp.height = 300.0f;
vp.xMin = 7600.0f; vp.xMax = 7999.0f;
vp.yMin = macd.yMin; vp.yMax = macd.yMax;

// 3. triangles out
std::vector<zplot::PlotVertex> verts;
zplot::AppendSeries(verts, macd, vp);

zplot::Viewport priceVp = zplot::FitViewport(bars, 0.0f, 0.0f, 800.0f, 500.0f, 400);
zplot::KLineStyle style;
zplot::AppendKLine(verts, bars, priceVp, style);
```

Measured output on 8000 bars:

```
vertices           : 244056
triangles          : 81352
bytes to upload    : 5857344
vertex layout      : 6 floats (24 bytes)
series in the plot : 5
```

## 5. Indicators

Three families, one entry point each:

```cpp
zplot::SeriesPlot p1 = zplot::ComputeTrend     (bars, "MA",   { 5, 10, 20, 30 });
zplot::SeriesPlot p2 = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
zplot::SeriesPlot p3 = zplot::ComputeVolume    (bars, "CJL",  { 5, 10 });
```

The full name list with formulas and terminal cross-check results is in  
[indicators.md](indicators.md).

### Failure is silent

There is no exception and no error code. A call that cannot be satisfied  
returns a `SeriesPlot` with `valid == false`:

```cpp
const zplot::SeriesPlot bad = zplot::ComputeOscillator(bars, "NOT_A_NAME", { 1 });
// bad.valid == false
```

**Always check `valid` before reading `series`, `yMin` or `yMax`.**

### The parameter-count trap

The most common way to get an empty chart. Every indicator has a **minimum  
parameter count**, and giving too few (or, for one indicator, *any*) parameters  
fails quietly. Verified by  
[`examples/02_indicators.cpp`](examples/02_indicators.cpp):

```
  SLOWKD   n=3  valid=false  3 params -> needs 4 (N, M1, M2, M3)
  SLOWKD   n=4  valid=true   4 params -> ok
  PSY      n=1  valid=false  1 param  -> needs 2 (period, maPeriod)
  PSY      n=2  valid=true   2 params -> ok
  BOLL     n=2  valid=false  2 params -> needs 3 (N, M, P)
  BOLL     n=3  valid=true   3 params -> ok
  PUBU     n=3  valid=false  3 params -> needs 6
  PUBU     n=6  valid=true   6 params -> ok
  ASl      n=1  valid=false  any param -> must be empty
  ASl      n=0  valid=true   no params -> ok
```

Two cases are worth calling out:

- **`ASl` takes no parameters at all.** Passing any makes it fail. Call it as  
  `ComputeOscillator(bars, "ASl", {})`.
- **`PUBU` requires six parameters but ignores their values.** The periods are  
  fixed internally at 4/6/9/13/18/24; the vector only has to be long enough.

All 59 indicators, computed on the same 400-bar set, resolve successfully:

```
Oscillators      24
Trend            21
Volume / OI      14
total=59  invalid=0
```

Four of them use a **Chinese API string** — easy to miss when scanning a code  
list, and they are the reason a sample that looks complete can still be  
under-counting:

```cpp
zplot::ComputeTrend (bars, "MA扩展", { 120, 240 });
zplot::ComputeTrend (bars, "唐奇安", { 20, 20 });
zplot::ComputeTrend (bars, "空",     { });          // draws nothing, valid == true
zplot::ComputeVolume(bars, "价量运行趋势", { 5, 10 });
```

### Reading a SeriesPlot back

A `SeriesPlot` is a list of named series plus the data range:

```
MACD breakdown
  series 0: prim=Bar  points=233  width=1.5 barWidth=4.0 barBase=0
  series 1: prim=Bar  points=142  width=1.5 barWidth=4.0 barBase=0
  series 2: prim=Line points=2    width=1.5 barWidth=4.0 barBase=0
  series 3: prim=Line points=375  width=1.5 barWidth=4.0 barBase=0
  series 4: prim=Line points=375  width=1.5 barWidth=4.0 barBase=0
  range: x=[25,399] y=[-22.3036,8.80445]
```

Note that MACD returns **five** series — histogram bars plus DIF/DEA and the  
zero axis — and the histogram uses `prim == Bar`, not `Line`.

## 6. Geometry

The triangulation helpers are public API. Use them for your own overlays  
(levels, markers, boxes) without touching the indicator code. All samples are  
in [`examples/03_geometry.cpp`](examples/03_geometry.cpp).

### Viewport mapping

`Viewport` is a linear map. The vertical axis is **flipped** — a larger value  
gives a smaller `y`, because pixel `y` grows downward.

```
  x(0)=0.00  x(50)=400.00  x(100)=800.00
  y(8000)=0.00  y(7000)=400.00   <- y is flipped
  dx()=8.000  dataWidth()=800.0
  degenerate: x(5)=10.0 y(5)=70.0 dx=0.0  (no NaN)
```

A degenerate range (`yMin == yMax`) is safe: it returns the bottom edge and  
`dx()` returns 0, so panning into a flat region never produces NaN vertices.

### Primitives

| Helper           | Produces                     | Triangles     |
| ---------------- | ---------------------------- | ------------- |
| `PushTriangle`   | one triangle                 | 1             |
| `PushQuadPoints` | arbitrary quad               | 2             |
| `PushRect`       | axis-aligned rectangle       | 2             |
| `PushSegment`    | one thick line segment       | 2             |
| `PushPoint`      | one square (scatter)         | 2             |
| `PushBar`        | a column from a baseline     | 2             |
| `PushPolyline`   | widened polyline + joints    | 2 per segment |
| `PushPolylineBy` | same, points from a callback | 2 per segment |

Measured vertex counts:

```
  PushTriangle             +3      -> 3 verts (1 tris)
  PushQuadPoints           +6      -> 9 verts (3 tris)
  PushRect                 +6      -> 15 verts (5 tris)
  PushSegment              +6      -> 21 verts (7 tris)
  PushPoint                +6      -> 27 verts (9 tris)
  PushBar                  +6      -> 33 verts (11 tris)
  PushSegment(degenerate)  +0      -> 33 verts (11 tris)
```

A zero-length segment contributes **0** vertices instead of degenerate  
triangles — safe to feed it duplicate points.

### Breaking a line

Insert a `NaN` where the line should break; the run is terminated and the next  
valid point starts a new one. Useful for indicators with a warm-up period.

```cpp
const float nan = std::numeric_limits<float>::quiet_NaN();
const float xy[] = { 0, 0, 10, 10, nan, nan, 20, 20, 30, 30 };
zplot::PushPolyline(v, xy, 5, 2.0f, color);   // two separate runs
```

`PushPolylineBy` generates points on demand — useful when materialising the  
whole array would be wasteful:

```cpp
zplot::PushPolylineBy(v, 20, [](std::size_t i, float& x, float& y) {
    x = static_cast<float>(i) * 4.0f;
    y = 50.0f + 30.0f * std::sin(static_cast<float>(i) * 0.5f);
    return true;      // return false to break the run
}, 1.5f, color);
```

### `AppendSeries`

Converts a whole `SeriesPlot` in one call, dispatching per series on  
`SeriesPrim`:

```
  AppendSeries(3 series)   +228    -> 567 verts (189 tris)
```

`AppendSeries` **appends** to the vector; it never clears it. That is what lets  
you accumulate several studies into one buffer before a single upload.

## 7. Colors, ranges and fitting

Sample: [`examples/04_theme_and_range.cpp`](examples/04_theme_and_range.cpp).

### The light-background trap

**The default palettes are tuned for a dark canvas.** Draw them on a light one  
and grey/white strokes wash out completely. `DimForLightBackground()` darkens  
exactly those, leaving saturated colors alone:

```
light-background trap
  series=4  bright(before)=1
    color=(1.000, 1.000, 1.000, 1.00)
    color=(1.000, 1.000, 0.000, 1.00)
    color=(0.500, 0.000, 1.000, 1.00)
    color=(0.000, 1.000, 0.000, 1.00)
  bright(after)=0
  vertices on a light canvas: 18444
  vertices on a dark canvas : 18444  (identical count: yes)
```


Call it once after computing, and only for a light canvas:

```cpp
zplot::SeriesPlot ma = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
ma.DimForLightBackground();     // no-op for a dark canvas
```

It changes colors only — never geometry — so the vertex count is identical  
either way.

### Ranges

```cpp
plot.PadRange(0.05f);       // widen the y range 5% at both ends
```

```
  MA raw          y=[7552.0366, 7654.4766]
  MA PadRange(5%) y=[7546.9146, 7659.5986]

  PriceRange      ok=true  y=[7540.96, 7668.24]
  PriceRange      ok=false (empty slice)
```

`PriceRange` returns `false` when nothing falls inside the slice — check it  
before using the outputs, which are left untouched in that case.

### `FitViewport`

Builds a viewport from the data, optionally showing only the last *N* bars:

```
  all bars : x=[0,399]   y=[7540.96,7668.24]
  last 100 : x=[300,399] y=[7548.52,7667.88]
  empty    : x=[0,1]     y=[0.00,1.00]   (no crash)
```

On an empty vector it returns a safe unit range rather than dividing by zero.

### Chart styles

```cpp
zplot::KLineStyle style;    // default: red up / cyan down, hollow up
zplot::AppendKLine    (v, bars, vp, style);
zplot::AppendBamboo   (v, bars, vp, style);
zplot::AppendCloseLine(v, bars, vp, style);
zplot::AppendTower    (v, bars, vp, style);
```

```
  AppendKLine      3606 verts (1202 tris)
  AppendBamboo     3600 verts (1200 tris)
  AppendCloseLine  1206 verts (402 tris)
  AppendTower      1200 verts (400 tris)
```

The default palette follows the Chinese convention: **red for up, cyan for  
down**. To switch to the western convention:

```cpp
zplot::KLineStyle western;
western.up      = zplot::Color4f::FromHex(0x1D9E75);   // green up
western.down    = zplot::Color4f::FromHex(0xE24B4A);   // red down
western.hollowUp = false;                              // solid bodies
```

## 8. Wiring the vertices into your renderer

Sample: [`examples/05_backend.cpp`](examples/05_backend.cpp).

### The vertex layout

```
sizeof(PlotVertex) = 24 bytes = 6 floats
offsets: x=0 y=4 r=8 g=12 b=16 a=20
```

It is exactly six tightly packed floats, asserted at compile time:

```cpp
static_assert(sizeof(PlotVertex) == 6 * sizeof(float), "...");
```

So the two attribute pointers are always:

| Attribute     | Size     | Offset  |
| ------------- | -------- | ------- |
| position (xy) | 2 floats | 0       |
| color (rgba)  | 4 floats | 8 bytes |

### No index buffer

Vertices come back to back as triangles, so a draw is one call:

```cpp
glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(zplot::PlotVertex),
             verts.data(), GL_STATIC_DRAW);
glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
```

Measured for a three-panel layout on 300 bars:

```
  price (KLine + MA)     drawArrays(GL_TRIANGLES, 0, 17250)   // 5750 tris
  volume (CJL)           drawArrays(GL_TRIANGLES, 0, 8832)   // 2944 tris
  oscillator (MACD)      drawArrays(GL_TRIANGLES, 0, 8256)   // 2752 tris
```

### Keeping panels aligned

**This is the mistake to avoid.** Every `SeriesPlot` carries the x range of the  
*data it was computed over*. Panels showing different windows will not line up  
unless you copy the x range from the price panel:

```cpp
const zplot::Viewport priceVp =
    zplot::FitViewport(bars, 0.0f, 0.0f, W, pricePanelH, 200);   // last 200 bars

zplot::SeriesPlot vol = zplot::ComputeVolume(bars, "CJL", { 5, 10 });

zplot::Viewport volVp;
volVp.left = 0.0f;
volVp.top = pricePanelH + 8.0f;
volVp.width = W;
volVp.height = volPanelH;
volVp.xMin = priceVp.xMin;      // NOT vol.xMin
volVp.xMax = priceVp.xMax;      // NOT vol.xMax
volVp.yMin = vol.yMin;
volVp.yMax = vol.yMax;
```

What the two choices actually produce:

```
  price panel x=[100,299]
  MA overlay  x=[100,299]  <- copied from price
  volume      x=[100,299]  <- copied from price (not from vol)
  oscillator  x=[100,299]  <- copied from price
  all four match: yes
  what vol.xMin/vol.xMax would have been: [0,299]
```

Using the volume plot's own range would have shown `[0,299]` — **100 bars of  
horizontal drift** against a price panel showing `[100,299]`.

Only `yMin`/`yMax` come from the panel's own plot; `xMin`/`xMax` are always  
inherited from the price panel.

### Drawing one series at a time

To toggle layers independently, upload per series:

```
  MA series 0            drawArrays(GL_TRIANGLES, 0, 3546)   // 1182 tris
  MA series 1            drawArrays(GL_TRIANGLES, 0, 3486)   // 1162 tris
  MA series 2            drawArrays(GL_TRIANGLES, 0, 3366)   // 1122 tris
  MA series 3            drawArrays(GL_TRIANGLES, 0, 3246)   // 1082 tris
```

## 9. Verifying the samples yourself

Every sample in this document is compiled **and executed** by one script:

```
docs/examples/verify-examples.ps1
```

It builds the four library sources, then compiles each `.cpp`, runs it, and  
counts a sample as verified only if it compiles clean **and exits 0**. The  
expected result is:

```
=== verifying samples ===
[ OK ] 01_quickstart
[ OK ] 02_indicators
[ OK ] 03_geometry
[ OK ] 04_theme_and_range
[ OK ] 05_backend

---------------- 5 ok, 0 failed ----------------
```

The script points at the toolchain directly instead of calling `vcvarsall.bat`,  
so it runs from a plain shell. Adjust the four paths at the top if your MSVC or  
Windows SDK lives elsewhere.

## 10. Common mistakes

| Symptom                                  | Cause                                              | Fix                                   |
| ---------------------------------------- | -------------------------------------------------- | ------------------------------------- |
| Empty chart, no error                    | Parameter count below the minimum                  | Check §5; verify `valid`              |
| Empty chart for `ASl`                    | You passed parameters                              | Pass `{}`                             |
| All series invisible on a light UI       | Default palette is dark-tuned                      | `DimForLightBackground()`             |
| Panels drift horizontally                | Panels use their own `xMin`/`xMax`                 | Copy the x range from the price panel |
| Chart shifted by a constant              | `Bar::index` does not start at 0                   | Renumber from 0                       |
| Gaps in the chart                        | `Bar::index` is not contiguous                     | Fill or renumber                      |
| `PlotVertex` misread in the shader       | Assuming a padded layout                           | It is 6 tightly packed floats         |
| Overlay vanished after adding a new plot | `Append*` appends but you cleared first            | Do not clear between appends          |
| NaN vertices                             | A `Viewport` was built by hand with `yMin == yMax` | Use `FitViewport` or `PadRange`       |



---

# Custom work / 定制服务

zplot is MIT and the free part stays free forever. Issues and PRs are always  
welcome.

If you need something built **on top of** zplot rather than inside it, I take  
contract work:

- **Indicator porting** — Pine Script, 通达信 or 文华 formulas into C++ or  
  MT5
- **K-line widget integration** — a chart wired into your existing app or  
  trading terminal
- **Indicator reconciliation** — proving your implementation matches a  
  reference terminal bar for bar

I build charting software. I do **not** take on discretionary trading, revenue  
sharing, or signal recommendation work, and I do not give investment advice.  
That is a legal line, not just a platform rule.

How to reach me: see [CONTACT.md](../CONTACT.md) in this repository.
