// SPDX-License-Identifier: MIT
/**
 * @file main.cpp
 * @brief zplot demo -- a self-contained Win32 + OpenGL 4.6 candlestick and indicator viewer.
 *
 * @details
 * A complete, dependency-free showcase of the zplot library:
 *
 *  - a plain Win32 window with a hand-rolled WGL OpenGL 4.6 core-profile context
 *    (no GLFW / SDL / GLAD / GLEW -- see ZeOpenGL.h);
 *  - every pixel is drawn as triangles fed straight from zplot's geometry helpers,
 *    which means the library's `zplot::PlotVertex` layout is uploaded as-is;
 *  - three stacked panels sharing one horizontal camera: candlesticks with an optional
 *    trend overlay, an oscillator sub-chart, and a volume / open-interest sub-chart;
 *  - 24 oscillators, 8 trend overlays and 13 volume studies are precomputed at start-up
 *    and cycled at runtime;
 *  - text comes from a GDI-built bitmap atlas (see ZeTextRenderer.h), so no font
 *    library is required either.
 *
 * Interaction:
 *   left-drag          pan the horizontal axis
 *   left click         cycle the study shown by the panel you clicked
 *                      (main panel = trend overlay, middle = oscillator, bottom = volume)
 *   right click        reset the view             (same as R)
 *   wheel              zoom around the cursor
 *   T                  next trend overlay
 *   M                  next main-chart style (candles / bamboo / close line / tower)
 *   V                  next volume study
 *   O                  next oscillator
 *   R                  reset the view
 *   Esc                quit
 *
 * Data: a CSV of `datetime,open,high,low,close,volume,open_oi,close_oi[,settle]`
 * rows. Pass a path as the first argument, or place a file next to the executable.
 *
 * @note No exchange API is touched anywhere in this file -- the demo is purely about
 *       turning bars into pictures. Everything the chart needs comes from the CSV.
 */

// windows.h defines min/max as macros, which breaks std::min / std::max, and rpcndr.h
// defines `small` as a macro, which breaks any identifier of that name. Turn both off
// before any system header is pulled in.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>
#include <ZeTrendIndicator.h>
#include <ZeVolumeIndicator.h>

#include "ZeOpenGL.h"
#include "ZeTextRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ===========================================================================
//  Palette (dark -- the indicator modules ship palettes tuned for a dark canvas)
// ===========================================================================

namespace pal {
    const zplot::Color4f kWindow  = zplot::Color4f::FromHex(0x0F1216);
    const zplot::Color4f kPanel   = zplot::Color4f::FromHex(0x14181E);
    const zplot::Color4f kGrid    = zplot::Color4f::FromHex(0x1D232B);
    const zplot::Color4f kGridMaj = zplot::Color4f::FromHex(0x2F3844);
    const zplot::Color4f kText    = zplot::Color4f::FromHex(0xE4E9F0);
    const zplot::Color4f kDim     = zplot::Color4f::FromHex(0x8792A2);
    const zplot::Color4f kAccent  = zplot::Color4f::FromHex(0x53A6FF);
}

// ===========================================================================
//  Small geometry types
// ===========================================================================

using zplot::PlotVertex;
using zplot::Viewport;

struct Rect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
};

/// @brief One horizontal band of the chart.
struct Panel {
    Rect  box;          ///< Full background rectangle, including the right-hand gutter
    Rect  plot;         ///< Data area (the box minus the gutter)
    float legendY = 0;  ///< Pixel y of this panel's legend line
};

// ===========================================================================
//  Data input and the study tables
// ===========================================================================

namespace {

const char* const kDefaultCsv = "CZCE_MA701_1min.csv";
constexpr int kInitialBars = 400;
constexpr int kMaxTailSeries = 6;

/// @brief Read a CSV of bars. Fixed column order, the header row is skipped.
std::vector<zplot::Bar> LoadBars(const std::string& path) {
    std::vector<zplot::Bar> bars;

    std::ifstream in(path, std::ios::binary);
    if (!in) return bars;

    std::string line;
    std::getline(in, line);                 // header (a UTF-8 BOM rides along and is dropped with it)

    int idx = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 12) continue;     // blank or truncated

        std::vector<std::string> f;
        std::string tok;
        std::stringstream ss(line);
        while (std::getline(ss, tok, ',')) f.push_back(tok);
        if (f.size() < 8) continue;

        zplot::Bar b;
        b.index = idx++;
        b.time = f[0];
        b.open = std::stod(f[1]);
        b.high = std::stod(f[2]);
        b.low = std::stod(f[3]);
        b.close = std::stod(f[4]);
        b.volume = std::stod(f[5]);
        b.openInterest = std::stod(f[7]);   // close_oi: what open-interest studies read
        // Column 9 (optional) is the settlement price; without it, fall back to the
        // close so settlement-driven studies do not degenerate into an empty chart.
        b.settle = (f.size() >= 9u && !f[8].empty()) ? std::stod(f[8]) : b.close;
        bars.push_back(std::move(b));
    }
    return bars;
}

/// @brief Directory part of a path (including the trailing separator), or "".
std::string DirOf(const std::string& path) {
    const std::size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos + 1);
}

struct StudySpec {
    const char*         name;
    std::vector<double> params;
};

/// @brief The 24 oscillator (sub-chart) studies.
const std::vector<StudySpec>& OscTable() {
    static const std::vector<StudySpec> kTable = {
        { "MACD",   { 12, 26, 9 } },            // fast / slow / signal
        { "KDJ",    { 9, 3, 3 } },              // RSV / K smoothing / D smoothing
        { "KD",     { 9, 3, 3 } },              // as above, without J
        { "ROC",    { 24, 20 } },               // ROC period / MA period
        { "RSI",    { 7, 14 } },                // two periods
        { "SLOWKD", { 9, 3, 3, 3 } },           // N / fast K / fast D / slow D
        { "WR",     { 14 } },                   // period
        { "BIAS",   { 6, 12, 24 } },            // three MA periods
        { "CR",     { 26, 5, 10, 20, 40 } },    // N plus four MAs
        { "ATR",    { 26 } },                   // period
        { "DMI",    { 14, 6 } },                // DI period / ADX period
        { "CCI",    { 14 } },                   // period
        { "PSY",    { 12, 6 } },                // PSY period / PSYMA period
        { "MTM",    { 6, 6 } },                 // MTM period / MA period
        { "DDI",    { 13, 30, 10, 5 } },        // SUM / SMA / weight / AD MA
        { "DMA",    { 10, 50, 10 } },           // N1 / N2 / signal
        { "ADTM",   { 23, 8 } },                // period / MA period
        { "ARBR",   { 26 } },                   // period
        { "LON",    { 10 } },                   // N (two recursive smoothings at N and 2N)
        { "SRDM",   { 30 } },                   // recursive smoothing period
        { "SHORT",  { 5 } },                    // period of the MA1 line
        { "MI",     { 12 } },                   // N (smoothing falls back to N)
        { "DPO",    { 20 } },                   // period (MA shifted forward N/2+1 bars)
        { "ASl",    {} },                       // takes no parameters
    };
    return kTable;
}

/// @brief Trend overlays drawn on top of the candlesticks (price units).
const std::vector<StudySpec>& MainTable() {
    static const std::vector<StudySpec> kTable = {
        { "MA",   { 5, 10, 20, 60 } },
        { "EMA",  { 5, 10, 20, 60 } },
        { "BOLL", { 26, 26, 2 } },              // MA period / stddev period / multiplier
        { "BBI",  { 3, 6, 12, 24 } },
        { "DKX",  { 10 } },
        { "SAR",  { 0.02, 0.2, 0.02 } },        // start AF / AF cap / AF step
        { "ENV",  { 14, 6 } },                  // MA period / percentage
        { "空",   {} },                         // draw no overlay (the API's "blank" name)
    };
    return kTable;
}

/// @brief Volume / open-interest studies for the bottom panel.
const std::vector<StudySpec>& VolTable() {
    static const std::vector<StudySpec> kTable = {
        { "CJL",  { 5, 10 } },
        { "MV",   { 5, 10 } },
        { "CCL",  { 5, 10 } },
        { "OPI",  {} },
        { "OBV",  {} },
        { "VR",   { 26 } },
        { "AD",   {} },
        { "PVT",  {} },
        { "WAD",  {} },
        { "WVAD", {} },
        { "VOSC", { 12, 26 } },
        { "VROC", { 12 } },
        { "VRSI", { 6 } },
    };
    return kTable;
}

std::string ParamsText(const std::vector<double>& p) {
    if (p.empty()) return "()";
    std::string s;
    char buf[32];
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (i) s += ",";
        std::snprintf(buf, sizeof(buf), "%.10g", p[i]);
        s += buf;
    }
    return s;
}

/// @brief ASCII label for the on-screen legend.
///
/// @details The bitmap atlas only covers ASCII 32..126, while a couple of study names
///          in the API are not ASCII (the "draw nothing" overlay, for instance). Map
///          those onto a readable ASCII label rather than letting them degrade to "?".
const char* DisplayName(const char* name) {
    return (std::strcmp(name, "空") == 0) ? "NONE" : name;
}

// ===========================================================================
//  Geometry helpers
// ===========================================================================

/**
 * @brief Crop a series to the visible x range, keeping two boundary points on each side.
 *
 * @details 8000 bars would produce a six-figure vertex count per frame. Keeping the
 *          points just outside the range (rather than exactly the ones inside) means
 *          the polyline stays continuous across the boundary; the per-panel scissor
 *          rectangle trims whatever spills over the edge.
 */
zplot::SeriesPlot ClipToRange(const zplot::SeriesPlot& src, float x0, float x1) {
    zplot::SeriesPlot out;
    out.valid = src.valid;
    out.xMin = src.xMin;   out.xMax = src.xMax;
    out.yMin = src.yMin;   out.yMax = src.yMax;
    out.series.reserve(src.series.size());

    const float lo = x0 - 2.0f;
    const float hi = x1 + 2.0f;

    for (const zplot::Series& s : src.series) {
        zplot::Series t;
        t.color = s.color;
        t.prim = s.prim;
        t.width = s.width;
        t.barWidth = s.barWidth;
        t.barBase = s.barBase;

        const std::size_t n = s.count();
        t.xy.reserve(n * 2);
        for (std::size_t i = 0; i < n; ++i) {
            const float x = s.xy[i * 2];
            if (x < lo || x > hi) continue;
            t.xy.push_back(x);
            t.xy.push_back(s.xy[i * 2 + 1]);
        }
        out.series.push_back(std::move(t));
    }
    return out;
}

/**
 * @brief Grow [lo, hi] so it also contains every finite value of `plot` inside the view.
 * @param any Whether [lo, hi] is meaningful already; the return value reports the new state.
 */
bool ExtendRangeByPlot(float& lo, float& hi, const zplot::SeriesPlot& plot,
    float xMin, float xMax, bool any) {

    if (!plot.valid || plot.series.empty()) return any;

    for (const zplot::Series& s : plot.series) {
        const std::size_t n = s.count();
        for (std::size_t i = 0; i < n; ++i) {
            const float x = s.xy[i * 2];
            if (x < xMin || x > xMax) continue;
            const float y = s.xy[i * 2 + 1];
            if (y != y) continue;                    // NaN = warm-up gap
            if (!any) { lo = hi = y; any = true; }
            else {
                if (y < lo) lo = y;
                if (y > hi) hi = y;
            }
        }
        // A bar series also occupies the space down to its baseline.
        if (any && s.prim == zplot::SeriesPrim::Bar) {
            if (s.barBase < lo) lo = s.barBase;
            if (s.barBase > hi) hi = s.barBase;
        }
    }
    return any;
}

/// @brief Add a symmetric margin, guarding against a zero-height range.
void PadRange(float& lo, float& hi, float ratio) {
    if (hi - lo < 1e-9f) { lo -= 0.5f; hi += 0.5f; }
    const float pad = (hi - lo) * ratio;
    lo -= pad;
    hi += pad;
}

/// @brief Pick a "nice" step (1/2/5 x 10^n) close to `rough`.
float NiceStep(float rough) {
    if (!(rough > 0.0f)) return 1.0f;
    const float mag = std::pow(10.0f, std::floor(std::log10(rough)));
    const float norm = rough / mag;
    float step;
    if (norm <= 1.0f)      step = 1.0f;
    else if (norm <= 2.0f) step = 2.0f;
    else if (norm <= 5.0f) step = 5.0f;
    else                   step = 10.0f;
    return step * mag;
}

/// @brief Decimal places that keep an axis label readable for the given step.
int DecimalsFor(float step) {
    if (step >= 100.0f) return 0;
    if (step >= 10.0f)  return 1;
    if (step >= 1.0f)   return 2;
    if (step >= 0.1f)   return 3;
    return 4;
}

const char* const kStyleNames[] = { "CANDLE", "BAMBOO", "CLOSE LINE", "TOWER" };
constexpr int kStyleCount = 4;

/// @brief Candlestick appearance for the selected main-chart style.
zplot::KLineStyle MakeStyle(int index) {
    zplot::KLineStyle style;          // red hollow up / cyan solid down / white flat
    if (index == 2) {                 // a close line only carries one color
        style.up = zplot::Color4f::FromHex(0x7FD4FF);
        style.down = style.up;
        style.flat = style.up;
    }
    return style;
}

} // namespace

// ===========================================================================
//  Application state
// ===========================================================================

struct Camera {
    float xMin = 0.0f, xMax = 1.0f;   // visible bar-index range, shared by every panel
    bool  dragging = false;
    float moved = 0.0f;               // travel since press, to tell a click from a drag
};

struct App {
    // ---- data ----
    std::vector<zplot::Bar>        bars;
    std::vector<zplot::SeriesPlot> osc;      // 24 oscillator studies
    std::vector<zplot::SeriesPlot> trend;    // 8 overlays
    std::vector<zplot::SeriesPlot> vol;      // 13 volume studies

    // ---- view state ----
    Camera cam;
    int    oscIndex = 0;
    int    trendIndex = 0;            // MA overlay by default
    int    volIndex = 0;
    int    styleIndex = 0;

    // ---- window / GL ----
    HWND   hwnd = nullptr;
    HDC    hdc = nullptr;
    int    width = 0, height = 0;
    int    lastMouseX = 0, lastMouseY = 0;
    bool   dirty = true;
    bool   quit = false;

    GLuint progSolid = 0, progText = 0;
    GLuint vaoSolid = 0, vboSolid = 0, vaoText = 0, vboText = 0;
    GLint  uSolidViewport = -1, uTextViewport = -1, uTextAtlas = -1;

    zegl::TextAtlas fontSmall;        // legends and hints
    zegl::TextAtlas fontBold;         // title and headline numbers

    // ---- per-frame CPU buffers ----
    Panel panels[3];
    bool  havePanels = false;

    std::vector<PlotVertex> vBg;      // panel backgrounds and grid
    std::vector<PlotVertex> vMain;    // candles + trend overlay
    std::vector<PlotVertex> vOsc;
    std::vector<PlotVertex> vVol;
    std::vector<zegl::TextVertex> vText;
};

// ===========================================================================
//  Scene construction
// ===========================================================================

namespace {

/// @brief Values at the last visible point of each series, for the on-screen readout.
std::string TailValues(const zplot::SeriesPlot& visible) {
    std::string s;
    char buf[48];
    int shown = 0;
    for (std::size_t i = 0; i < visible.series.size() && shown < kMaxTailSeries; ++i) {
        const zplot::Series& ser = visible.series[i];
        const std::size_t n = ser.count();
        if (n == 0) continue;
        const float y = ser.xy[n * 2 - 1];
        if (y != y) continue;                       // NaN tail
        std::snprintf(buf, sizeof(buf), "  [%zu]=%.4g", i, y);
        s += buf;
        ++shown;
    }
    return s;
}

/// @brief Draw horizontal grid lines with nice round labels in the right-hand gutter.
void AppendValueAxis(std::vector<PlotVertex>& out,
    std::vector<zegl::TextVertex>& text, const zegl::TextAtlas& font,
    const Panel& panel, float yMin, float yMax) {

    const float span = yMax - yMin;
    if (!(span > 0.0f)) return;

    const float step = NiceStep(span / 4.0f);
    const int   decimals = DecimalsFor(step);
    const float first = std::ceil(yMin / step) * step;

    char label[32];
    for (float v = first; v <= yMax + step * 0.001f; v += step) {
        const float y = panel.plot.y0 + (yMax - v) / span * panel.plot.h();
        if (y < panel.plot.y0 - 0.5f || y > panel.plot.y1 + 0.5f) continue;

        zplot::PushRect(out, panel.plot.x0, y, panel.plot.x1, y + 1.0f, pal::kGrid);

        std::snprintf(label, sizeof(label), "%.*f", decimals, v);
        font.Draw(text, panel.box.x1 + 8.0f, y - font.lineHeight() * 0.5f,
            label, pal::kDim);
    }
}

void BuildScene(App& app) {
    app.vBg.clear();
    app.vMain.clear();
    app.vOsc.clear();
    app.vVol.clear();
    app.vText.clear();
    app.havePanels = false;

    const float W = static_cast<float>(app.width);
    const float H = static_cast<float>(app.height);
    if (W < 96.0f || H < 120.0f) return;

    // ---- panel layout -----------------------------------------------------
    const float gutter  = 70.0f;    // right-hand strip for value labels
    const float headerH = 20.0f;    // legend strip above each sub-panel
    const float gap     = 6.0f;
    const float titleH  = 36.0f;    // title block at the top
    const float hintH   = 26.0f;    // hint line at the bottom

    const float plotRight = W - gutter;
    const float freeH = H - titleH - hintH - 2.0f * gap - 2.0f * headerH;
    if (freeH < 90.0f || plotRight < 32.0f) return;

    const float mainH = freeH * 0.60f;
    const float oscH  = freeH * 0.25f;
    const float volH  = freeH - mainH - oscH;

    Panel& main = app.panels[0];
    Panel& oscP = app.panels[1];
    Panel& volP = app.panels[2];

    main.box = { 0.0f, titleH, plotRight, titleH + mainH };
    oscP.box = { 0.0f, main.box.y1 + gap + headerH, plotRight, main.box.y1 + gap + headerH + oscH };
    volP.box = { 0.0f, oscP.box.y1 + gap + headerH, plotRight, oscP.box.y1 + gap + headerH + volH };

    main.plot = main.box;
    oscP.plot = oscP.box;
    volP.plot = volP.box;

    main.legendY = main.box.y0 + 4.0f;                 // inside the main panel
    oscP.legendY = oscP.box.y0 - headerH + 2.0f;       // in the strip above
    volP.legendY = volP.box.y0 - headerH + 2.0f;
    app.havePanels = true;

    // ---- visible range ----------------------------------------------------
    const float xMin = app.cam.xMin;
    const float xMax = app.cam.xMax;

    const std::vector<StudySpec>& oscTable = OscTable();
    const std::vector<StudySpec>& mainTable = MainTable();
    const std::vector<StudySpec>& volTable = VolTable();

    const StudySpec& oscSpec = oscTable[static_cast<std::size_t>(app.oscIndex)];
    const StudySpec& trdSpec = mainTable[static_cast<std::size_t>(app.trendIndex)];
    const StudySpec& volSpec = volTable[static_cast<std::size_t>(app.volIndex)];

    const zplot::SeriesPlot& oscFull = app.osc[static_cast<std::size_t>(app.oscIndex)];
    const zplot::SeriesPlot& trdFull = app.trend[static_cast<std::size_t>(app.trendIndex)];
    const zplot::SeriesPlot& volFull = app.vol[static_cast<std::size_t>(app.volIndex)];

    // ---- backgrounds ------------------------------------------------------
    zplot::PushRect(app.vBg, 0.0f, 0.0f, W, H, pal::kWindow);
    zplot::PushRect(app.vBg, main.box.x0, main.box.y0, main.box.x1, main.box.y1, pal::kPanel);
    zplot::PushRect(app.vBg, oscP.box.x0, oscP.box.y0, oscP.box.x1, oscP.box.y1, pal::kPanel);
    zplot::PushRect(app.vBg, volP.box.x0, volP.box.y0, volP.box.x1, volP.box.y1, pal::kPanel);

    // ---- vertical grid across every panel --------------------------------
    {
        const float span = xMax - xMin;
        if (span > 1.0f) {
            const float step = NiceStep(span / 8.0f);
            const float first = std::ceil(xMin / step) * step;
            for (float v = first; v <= xMax; v += step) {
                const float x = main.plot.x0 + (v - xMin) / span * main.plot.w();
                for (const Panel* p : { &main, &oscP, &volP }) {
                    zplot::PushRect(app.vBg, x, p->box.y0, x + 1.0f, p->box.y1, pal::kGrid);
                }
            }
        }
    }

    // =======================================================================
    //  Main panel: candlesticks plus an optional trend overlay
    // =======================================================================

    float lo = 0.0f, hi = 0.0f;
    bool any = zplot::PriceRange(app.bars, xMin, xMax, 0.0f, lo, hi);
    any = ExtendRangeByPlot(lo, hi, trdFull, xMin, xMax, any);
    if (!any) { lo = 0.0f; hi = 1.0f; }
    PadRange(lo, hi, 0.06f);

    Viewport vpMain;
    vpMain.left = main.plot.x0;   vpMain.top = main.plot.y0;
    vpMain.width = main.plot.w(); vpMain.height = main.plot.h();
    vpMain.xMin = xMin;           vpMain.xMax = xMax;
    vpMain.yMin = lo;             vpMain.yMax = hi;

    {
        const zplot::KLineStyle style = MakeStyle(app.styleIndex);
        switch (app.styleIndex) {
        case 1:  zplot::AppendBamboo(app.vMain, app.bars, vpMain, style); break;
        case 2:  zplot::AppendCloseLine(app.vMain, app.bars, vpMain, style); break;
        case 3:  zplot::AppendTower(app.vMain, app.bars, vpMain, style); break;
        default: zplot::AppendKLine(app.vMain, app.bars, vpMain, style); break;
        }
    }

    zplot::SeriesPlot trdVisible;
    if (trdFull.valid && !trdFull.series.empty()) {
        trdVisible = ClipToRange(trdFull, xMin, xMax);
        zplot::AppendSeries(app.vMain, trdVisible, vpMain);
    }

    AppendValueAxis(app.vBg, app.vText, app.fontSmall, main, lo, hi);
    app.fontSmall.Draw(app.vText, main.box.x1 + 8.0f, main.box.y0 + 4.0f, "PRICE", pal::kAccent);

    // =======================================================================
    //  Oscillator panel
    // =======================================================================

    float olo = 0.0f, ohi = 0.0f;
    bool oany = ExtendRangeByPlot(olo, ohi, oscFull, xMin, xMax, false);
    if (!oany) { olo = -1.0f; ohi = 1.0f; }
    PadRange(olo, ohi, 0.08f);

    Viewport vpOsc;
    vpOsc.left = oscP.plot.x0;   vpOsc.top = oscP.plot.y0;
    vpOsc.width = oscP.plot.w(); vpOsc.height = oscP.plot.h();
    vpOsc.xMin = xMin;           vpOsc.xMax = xMax;
    vpOsc.yMin = olo;            vpOsc.yMax = ohi;

    zplot::SeriesPlot oscVisible;
    if (oscFull.valid && !oscFull.series.empty()) {
        oscVisible = ClipToRange(oscFull, xMin, xMax);
        zplot::AppendSeries(app.vOsc, oscVisible, vpOsc);
    }

    if (olo < 0.0f && ohi > 0.0f) {                 // zero line for signed oscillators
        const float yz = vpOsc.y(0.0f);
        zplot::PushRect(app.vOsc, oscP.plot.x0, yz, oscP.plot.x1, yz + 1.0f, pal::kGridMaj);
    }

    AppendValueAxis(app.vBg, app.vText, app.fontSmall, oscP, olo, ohi);

    // =======================================================================
    //  Volume panel
    // =======================================================================

    float vlo = 0.0f, vhi = 0.0f;
    bool vany = ExtendRangeByPlot(vlo, vhi, volFull, xMin, xMax, false);
    if (!vany) { vlo = 0.0f; vhi = 1.0f; }
    // Volume studies that stay positive read better anchored at zero.
    if (vlo > 0.0f && vhi > 0.0f) vlo = 0.0f;
    if (vhi < 0.0f && vlo < 0.0f) vhi = 0.0f;
    PadRange(vlo, vhi, 0.08f);

    Viewport vpVol;
    vpVol.left = volP.plot.x0;   vpVol.top = volP.plot.y0;
    vpVol.width = volP.plot.w(); vpVol.height = volP.plot.h();
    vpVol.xMin = xMin;           vpVol.xMax = xMax;
    vpVol.yMin = vlo;            vpVol.yMax = vhi;

    zplot::SeriesPlot volVisible;
    if (volFull.valid && !volFull.series.empty()) {
        volVisible = ClipToRange(volFull, xMin, xMax);
        zplot::AppendSeries(app.vVol, volVisible, vpVol);
    }

    AppendValueAxis(app.vBg, app.vText, app.fontSmall, volP, vlo, vhi);

    // =======================================================================
    //  Text overlays
    // =======================================================================

    const zegl::TextAtlas& fntBold = app.fontBold;
    const zegl::TextAtlas& fntSmall = app.fontSmall;

    {
        char line[192];
        std::snprintf(line, sizeof(line), "zplot demo     %d bars     1 min",
            static_cast<int>(app.bars.size()));
        fntBold.Draw(app.vText, 10.0f, 6.0f, line, pal::kText);
    }
    {
        char line[288];
        const std::string first = app.bars.empty() ? "" : app.bars.front().time;
        const std::string last = app.bars.empty() ? "" : app.bars.back().time;
        std::snprintf(line, sizeof(line), "%s  ~  %s        style %s        view %.0f bars",
            first.c_str(), last.c_str(), kStyleNames[app.styleIndex], xMax - xMin);
        fntSmall.Draw(app.vText, 10.0f, 26.0f, line, pal::kDim);
    }
    {
        char line[320];
        const std::string tail = TailValues(trdVisible);
        std::snprintf(line, sizeof(line), "overlay  %s %s%s",
            DisplayName(trdSpec.name), ParamsText(trdSpec.params).c_str(), tail.c_str());
        fntSmall.Draw(app.vText, 10.0f, main.legendY, line, pal::kAccent);
    }
    {
        char line[352];
        const std::string tail = TailValues(oscVisible);
        std::snprintf(line, sizeof(line), "oscillator  %s %s   (%d/%d)%s",
            DisplayName(oscSpec.name), ParamsText(oscSpec.params).c_str(),
            app.oscIndex + 1, static_cast<int>(oscTable.size()), tail.c_str());
        fntSmall.Draw(app.vText, 10.0f, oscP.legendY, line, pal::kText);
    }
    {
        char line[352];
        const std::string tail = TailValues(volVisible);
        std::snprintf(line, sizeof(line), "volume  %s %s   (%d/%d)%s",
            DisplayName(volSpec.name), ParamsText(volSpec.params).c_str(),
            app.volIndex + 1, static_cast<int>(volTable.size()), tail.c_str());
        fntSmall.Draw(app.vText, 10.0f, volP.legendY, line, pal::kText);
    }
    {
        const char* hint =
            "drag = pan    wheel = zoom    click a panel = next study in it    "
            "T / M / V = overlay / style / volume    R = reset    Esc = quit";
        fntSmall.Draw(app.vText, 10.0f, H - 18.0f, hint, pal::kDim);
    }
}

} // namespace

// ===========================================================================
//  Rendering
// ===========================================================================

namespace {

void UploadAndDraw(GLuint vao, GLuint vbo, const std::vector<PlotVertex>& verts) {
    if (verts.empty()) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(PlotVertex)),
        verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

void UploadAndDrawText(GLuint vao, GLuint vbo, const std::vector<zegl::TextVertex>& verts) {
    if (verts.empty()) return;
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(zegl::TextVertex)),
        verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
}

/// @brief Turn a panel rect (top-left origin) into a scissor box (bottom-left origin).
void ScissorPanel(const Panel& p, int fbHeight) {
    const int x = static_cast<int>(p.box.x0);
    const int y = static_cast<int>(p.box.y0);
    const int w = static_cast<int>(p.box.w() + 0.5f);
    const int h = static_cast<int>(p.box.h() + 0.5f);
    if (w > 0 && h > 0) glScissor(x, fbHeight - (y + h), w, h);
}

void Render(App& app) {
    if (app.width <= 0 || app.height <= 0) return;

    glViewport(0, 0, app.width, app.height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(pal::kWindow.r, pal::kWindow.g, pal::kWindow.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float W = static_cast<float>(app.width);
    const float H = static_cast<float>(app.height);

    glUseProgram(app.progSolid);
    glUniform2f(app.uSolidViewport, W, H);

    // Backgrounds and grid sit exactly inside their panels, so they need no scissor.
    UploadAndDraw(app.vaoSolid, app.vboSolid, app.vBg);

    if (app.havePanels) {
        glEnable(GL_SCISSOR_TEST);
        ScissorPanel(app.panels[0], app.height);
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vMain);
        ScissorPanel(app.panels[1], app.height);
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vOsc);
        ScissorPanel(app.panels[2], app.height);
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vVol);
        glDisable(GL_SCISSOR_TEST);
    }
    else {
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vMain);
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vOsc);
        UploadAndDraw(app.vaoSolid, app.vboSolid, app.vVol);
    }

    if (!app.vText.empty() && app.fontSmall.valid()) {
        glUseProgram(app.progText);
        glUniform2f(app.uTextViewport, W, H);
        glUniform1i(app.uTextAtlas, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app.fontSmall.texture());
        UploadAndDrawText(app.vaoText, app.vboText, app.vText);
    }

    glUseProgram(0);
    zegl::ZeGLSwapBuffers(app.hdc);
}

/// @brief Build the scene and present it.
void Present(App& app) {
    BuildScene(app);
    Render(app);
    app.dirty = false;
}

} // namespace

// ===========================================================================
//  Shaders
// ===========================================================================

namespace {

const char* const kSolidVS = R"(#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
uniform vec2 uViewport;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uViewport.x * 2.0 - 1.0,
                    1.0 - aPos.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)";

const char* const kSolidFS = R"(#version 460 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

const char* const kTextVS = R"(#version 460 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewport;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uViewport.x * 2.0 - 1.0,
                    1.0 - aPos.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* const kTextFS = R"(#version 460 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
    float coverage = texture(uAtlas, vUV).r;
    FragColor = vec4(vColor.rgb, vColor.a * coverage);
}
)";

} // namespace

// ===========================================================================
//  View manipulation
// ===========================================================================

namespace {

void ResetView(App& app) {
    const float last = static_cast<float>(app.bars.empty() ? 0 : app.bars.size() - 1);
    const float count = std::min(static_cast<float>(kInitialBars), last + 1.0f);
    app.cam.xMin = std::max(0.0f, last - count + 1.0f);
    app.cam.xMax = last;
}

void ClampCamera(App& app) {
    const float n = static_cast<float>(app.bars.size());
    const float last = n > 0.0f ? n - 1.0f : 0.0f;

    float span = app.cam.xMax - app.cam.xMin;
    const float minSpan = 12.0f;
    const float maxSpan = n + 40.0f;

    if (span < minSpan) span = minSpan;
    if (span > maxSpan) span = maxSpan;

    if (app.cam.xMax - app.cam.xMin != span) {
        const float mid = (app.cam.xMin + app.cam.xMax) * 0.5f;
        app.cam.xMin = mid - span * 0.5f;
        app.cam.xMax = mid + span * 0.5f;
    }

    // Never let the visible window drift entirely off the data.
    const float slack = span;
    if (app.cam.xMin > last + slack) {
        app.cam.xMax -= (app.cam.xMin - (last + slack));
        app.cam.xMin = last + slack;
    }
    if (app.cam.xMax < -slack) {
        app.cam.xMin += (-slack - app.cam.xMax);
        app.cam.xMax = -slack;
    }
}

float PlotWidth(const App& app) {
    return std::max(1.0f, static_cast<float>(app.width) - 70.0f);
}

void PanByPixels(App& app, float dxPixels) {
    const float dx = dxPixels * (app.cam.xMax - app.cam.xMin) / PlotWidth(app);
    app.cam.xMin -= dx;
    app.cam.xMax -= dx;
    ClampCamera(app);
}

void ZoomAt(App& app, float cursorX, bool zoomIn) {
    const float t = std::min(1.0f, std::max(0.0f, cursorX / PlotWidth(app)));

    const float k = zoomIn ? 0.85f : (1.0f / 0.85f);
    const float span = app.cam.xMax - app.cam.xMin;
    const float dataX = app.cam.xMin + t * span;

    const float newSpan = span * k;
    app.cam.xMin = dataX - t * newSpan;
    app.cam.xMax = app.cam.xMin + newSpan;
    ClampCamera(app);
}

} // namespace

// ===========================================================================
//  Window procedure
// ===========================================================================

namespace {

App* g_app = nullptr;

void NextOscillator(App& app) {
    app.oscIndex = (app.oscIndex + 1) % static_cast<int>(OscTable().size());
}

/// @brief Which panel a client-coordinate point falls into, or -1 for none.
///
/// @details Hit testing walks the panels from the top down and compares against the
///          bottom edge only, so the legend strip and the gap between two panels count
///          as belonging to the panel *below* them -- clicking a legend switches the
///          study that legend describes.
int PanelIndexAt(const App& app, int my) {
    if (!app.havePanels) return -1;
    for (int i = 0; i < 3; ++i) {
        if (static_cast<float>(my) <= app.panels[i].box.y1) return i;
    }
    return -1;
}

/// @brief Cycle the study shown by one panel (0 = main overlay, 1 = oscillator, 2 = volume).
void NextStudyIn(App& app, int panel) {
    switch (panel) {
    case 0: app.trendIndex = (app.trendIndex + 1) % static_cast<int>(MainTable().size()); break;
    case 1: app.oscIndex = (app.oscIndex + 1) % static_cast<int>(OscTable().size()); break;
    case 2: app.volIndex = (app.volIndex + 1) % static_cast<int>(VolTable().size()); break;
    default: return;
    }
    app.dirty = true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;
    if (!app) return ::DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_SIZE:
        app->width = LOWORD(lParam);
        app->height = HIWORD(lParam);
        app->dirty = true;
        return 0;

    case WM_LBUTTONDOWN:
        app->cam.dragging = true;
        app->cam.moved = 0.0f;
        app->lastMouseX = static_cast<short>(LOWORD(lParam));
        app->lastMouseY = static_cast<short>(HIWORD(lParam));
        ::SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE: {
        const int mx = static_cast<short>(LOWORD(lParam));
        const int my = static_cast<short>(HIWORD(lParam));
        if (app->cam.dragging) {
            const float dx = static_cast<float>(mx - app->lastMouseX);
            const float dy = static_cast<float>(my - app->lastMouseY);
            app->cam.moved += std::fabs(dx) + std::fabs(dy);
            if (dx != 0.0f) { PanByPixels(*app, dx); app->dirty = true; }
        }
        app->lastMouseX = mx;
        app->lastMouseY = my;
        return 0;
    }

    case WM_LBUTTONUP:
        app->cam.dragging = false;
        ::ReleaseCapture();
        if (app->cam.moved < 4.0f) {          // a click, not a drag
            NextStudyIn(*app, PanelIndexAt(*app, static_cast<short>(HIWORD(lParam))));
        }
        return 0;

    case WM_RBUTTONUP:
        ResetView(*app);
        app->dirty = true;
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        ::ScreenToClient(hwnd, &pt);
        ZoomAt(*app, static_cast<float>(pt.x), GET_WHEEL_DELTA_WPARAM(wParam) > 0);
        app->dirty = true;
        return 0;
    }

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE: ::PostMessageA(hwnd, WM_CLOSE, 0, 0); return 0;
        case 'O': NextOscillator(*app); break;
        case 'T':
            app->trendIndex = (app->trendIndex + 1) % static_cast<int>(MainTable().size());
            break;
        case 'V':
            app->volIndex = (app->volIndex + 1) % static_cast<int>(VolTable().size());
            break;
        case 'M': app->styleIndex = (app->styleIndex + 1) % kStyleCount; break;
        case 'R': ResetView(*app); break;
        default: return 0;
        }
        app->dirty = true;
        return 0;

    case WM_ERASEBKGND:
        return 1;                       // painted by Render(), no GDI erase wanted

    case WM_PAINT: {
        PAINTSTRUCT ps;
        ::BeginPaint(hwnd, &ps);
        Present(*app);
        ::EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        ::DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        app->quit = true;
        ::PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return ::DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

// ===========================================================================
//  Entry point
// ===========================================================================

int main(int argc, char** argv) {
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetProcessDPIAware();

    App app;

    // ---- locate the bar data ---------------------------------------------
    std::string exeDir;
    {
        char buf[MAX_PATH] = {};
        if (::GetModuleFileNameA(nullptr, buf, MAX_PATH)) exeDir = DirOf(buf);
    }

    const std::string requested = (argc > 1 && argv[1] && argv[1][0]) ? argv[1] : kDefaultCsv;
    app.bars = LoadBars(requested);

    if (app.bars.empty() && !exeDir.empty()) {
        // Next to the executable, then a few steps up into the repository layout.
        const char* const kRel[] = {
            "", "..\\", "..\\..\\tools\\data\\", "..\\tools\\data\\", "tools\\data\\"
        };
        for (const char* rel : kRel) {
            app.bars = LoadBars(exeDir + rel + kDefaultCsv);
            if (!app.bars.empty()) break;
        }
    }

    if (app.bars.empty()) {
        std::printf("no bar data found (looked for '%s').\n", requested.c_str());
        std::printf("usage: zplot_demo.exe [bars.csv]\n");
        return 2;
    }

    std::printf("loaded %d bars: %s ~ %s\n",
        static_cast<int>(app.bars.size()),
        app.bars.front().time.c_str(), app.bars.back().time.c_str());

    // ---- precompute every study ------------------------------------------
    const std::vector<StudySpec>& oscTable = OscTable();
    const std::vector<StudySpec>& mainTable = MainTable();
    const std::vector<StudySpec>& volTable = VolTable();

    app.osc.reserve(oscTable.size());
    for (const StudySpec& s : oscTable) {
        app.osc.push_back(zplot::ComputeOscillator(app.bars, s.name, s.params));
    }
    app.trend.reserve(mainTable.size());
    for (const StudySpec& s : mainTable) {
        app.trend.push_back(zplot::ComputeTrend(app.bars, s.name, s.params));
    }
    app.vol.reserve(volTable.size());
    for (const StudySpec& s : volTable) {
        app.vol.push_back(zplot::ComputeVolume(app.bars, s.name, s.params));
    }

    // Note: the shipped palettes are tuned for a dark canvas, which is what this demo
    // uses, so DimForLightBackground() is deliberately not called. Switch the palette
    // above to a light theme and you would want it.

    // ---- window -----------------------------------------------------------
    const char* kClass = "ZePlotDemoWindow";
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = ::GetModuleHandleA(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    if (!::RegisterClassExA(&wc)) {
        std::printf("RegisterClassExA failed (%lu)\n", ::GetLastError());
        return 3;
    }

    RECT rc{ 0, 0, 1280, 800 };
    ::AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    app.hwnd = ::CreateWindowExA(0, kClass, "zplot demo  -  candlestick & indicator viewer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd) {
        std::printf("CreateWindowExA failed (%lu)\n", ::GetLastError());
        return 3;
    }

    g_app = &app;

    // ---- OpenGL 4.6 core profile -----------------------------------------
    {
        zegl::GLConfig cfg;
        cfg.major = 4; cfg.minor = 6; cfg.samples = 4; cfg.core = true;

        std::string err;
        HGLRC gl = zegl::ZeGLCreateContext(app.hwnd, cfg, err);
        if (!gl) {
            std::printf("OpenGL init failed: %s\n", err.c_str());
            ::MessageBoxA(nullptr, err.c_str(), "OpenGL context creation failed", MB_ICONERROR);
            return 4;
        }
        app.hdc = ::GetDC(app.hwnd);

        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        std::printf("OpenGL %s   %s\n", version ? version : "?", renderer ? renderer : "?");
        std::fflush(stdout);
    }

    // ---- shaders, buffers and fonts --------------------------------------
    {
        std::string err;
        app.progSolid = zegl::ZeGLBuildProgram(kSolidVS, kSolidFS, err);
        if (!app.progSolid) { std::printf("solid shader failed:\n%s\n", err.c_str()); return 5; }

        app.progText = zegl::ZeGLBuildProgram(kTextVS, kTextFS, err);
        if (!app.progText) { std::printf("text shader failed:\n%s\n", err.c_str()); return 5; }

        app.uSolidViewport = glGetUniformLocation(app.progSolid, "uViewport");
        app.uTextViewport = glGetUniformLocation(app.progText, "uViewport");
        app.uTextAtlas = glGetUniformLocation(app.progText, "uAtlas");

        // Solid geometry: zplot::PlotVertex is exactly { x, y, r, g, b, a } with no padding,
        // so the vertex array is uploaded verbatim.
        glGenVertexArrays(1, &app.vaoSolid);
        glGenBuffers(1, &app.vboSolid);
        glBindVertexArray(app.vaoSolid);
        glBindBuffer(GL_ARRAY_BUFFER, app.vboSolid);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PlotVertex),
            reinterpret_cast<const void*>(offsetof(PlotVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PlotVertex),
            reinterpret_cast<const void*>(offsetof(PlotVertex, r)));
        glBindVertexArray(0);

        glGenVertexArrays(1, &app.vaoText);
        glGenBuffers(1, &app.vboText);
        glBindVertexArray(app.vaoText);
        glBindBuffer(GL_ARRAY_BUFFER, app.vboText);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(zegl::TextVertex),
            reinterpret_cast<const void*>(offsetof(zegl::TextVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(zegl::TextVertex),
            reinterpret_cast<const void*>(offsetof(zegl::TextVertex, u)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(zegl::TextVertex),
            reinterpret_cast<const void*>(offsetof(zegl::TextVertex, r)));
        glBindVertexArray(0);

        if (!app.fontSmall.Build(13, false, "Consolas", err)) {
            std::printf("font atlas failed: %s\n", err.c_str());
            return 5;
        }
        if (!app.fontBold.Build(16, true, "Consolas", err)) {
            std::printf("font atlas failed: %s\n", err.c_str());
            return 5;
        }
    }

    // ---- show and run -----------------------------------------------------
    ResetView(app);

    ::GetClientRect(app.hwnd, &rc);
    app.width = static_cast<int>(rc.right - rc.left);
    app.height = static_cast<int>(rc.bottom - rc.top);

    ::ShowWindow(app.hwnd, SW_SHOW);
    ::UpdateWindow(app.hwnd);

    // Render on demand: the scene only changes when the view or a study changes, so
    // there is no reason to burn a frame every vsync.
    bool running = true;
    while (running && !app.quit) {
        MSG msg;
        while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            ::TranslateMessage(&msg);
            ::DispatchMessageA(&msg);
        }
        if (!running || app.quit) break;

        if (app.dirty) {
            Present(app);
        }
        else {
            ::WaitMessage();
        }
    }

    // ---- teardown ---------------------------------------------------------
    if (app.vaoSolid) glDeleteVertexArrays(1, &app.vaoSolid);
    if (app.vboSolid) glDeleteBuffers(1, &app.vboSolid);
    if (app.vaoText)  glDeleteVertexArrays(1, &app.vaoText);
    if (app.vboText)  glDeleteBuffers(1, &app.vboText);
    app.fontSmall.Destroy();
    app.fontBold.Destroy();
    if (app.progSolid) glDeleteProgram(app.progSolid);
    if (app.progText)  glDeleteProgram(app.progText);

    return 0;
}
