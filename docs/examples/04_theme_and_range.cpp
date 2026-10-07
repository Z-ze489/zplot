// =====================================================================
//  04_theme_and_range.cpp
//  Sample for "Colors, ranges and viewport fitting" in docs/development.md
//
//  The trap this sample exists to document: the default palette is tuned
//  for a DARK canvas. Draw it on a light one and the white/grey strokes
//  disappear. DimForLightBackground() is the fix.
// =====================================================================

#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>
#include <ZeTrendIndicator.h>
#include <color4f.h>

#include <cstdio>
#include <vector>

static std::vector<zplot::Bar> LoadBars(int n) {
    std::vector<zplot::Bar> bars;
    bars.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double base = 7600.0 + 40.0 * ((i % 120) - 60) / 60.0 + 0.02 * i;
        zplot::Bar b;
        b.index = i;
        b.time = "2026-10-07 09:00";
        b.open = base;
        b.high = base + 12.0 + (i % 7);
        b.low = base - 12.0 - (i % 5);
        b.close = base + ((i % 3) - 1) * 4.0;
        b.volume = 1000.0 + (i % 500);
        b.openInterest = 50000.0 + (i % 900);
        b.settle = b.close;
        bars.push_back(b);
    }
    return bars;
}

// Counts how many series would vanish on a light background: any series
// whose darkest channel is above 0.5 is a bright stroke.
static int BrightSeries(const zplot::SeriesPlot& p) {
    int bright = 0;
    for (const zplot::Series& s : p.series) {
        float mn = s.color.r;
        if (s.color.g < mn) mn = s.color.g;
        if (s.color.b < mn) mn = s.color.b;
        if (mn > 0.5f) ++bright;
    }
    return bright;
}

int main() {
    const std::vector<zplot::Bar> bars = LoadBars(400);

    // ---- 1. the light-background trap -----------------------------------
    std::printf("light-background trap\n");
    zplot::SeriesPlot ma = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
    if (!ma.valid) { std::printf("  MA invalid\n"); return 1; }

    std::printf("  series=%zu  bright(before)=%d\n",
        ma.series.size(), BrightSeries(ma));
    for (const zplot::Series& s : ma.series) {
        std::printf("    color=(%.3f, %.3f, %.3f, %.2f)\n",
            s.color.r, s.color.g, s.color.b, s.color.a);
    }

    ma.DimForLightBackground();
    std::printf("  bright(after)=%d\n", BrightSeries(ma));

    std::vector<zplot::PlotVertex> lightVerts;
    zplot::Viewport lv;
    lv.left = 0; lv.top = 0; lv.width = 800; lv.height = 300;
    lv.xMin = ma.xMin; lv.xMax = ma.xMax; lv.yMin = ma.yMin; lv.yMax = ma.yMax;
    zplot::AppendSeries(lightVerts, ma, lv);
    std::printf("  vertices on a light canvas: %zu\n", lightVerts.size());

    // On a dark canvas the same series is left alone.
    zplot::SeriesPlot maDark = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
    std::vector<zplot::PlotVertex> darkVerts;
    zplot::AppendSeries(darkVerts, maDark, lv);
    std::printf("  vertices on a dark canvas : %zu  (identical count: %s)\n",
        darkVerts.size(),
        darkVerts.size() == lightVerts.size() ? "yes" : "no");

    // ---- 2. ranges ------------------------------------------------------
    std::printf("\nranges\n");
    std::printf("  MA raw          y=[%.4f, %.4f]\n", ma.yMin, ma.yMax);
    zplot::SeriesPlot padded = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
    padded.PadRange(0.05f);
    std::printf("  MA PadRange(5%%) y=[%.4f, %.4f]\n", padded.yMin, padded.yMax);

    float pmin = 0.0f, pmax = 0.0f;
    const bool got = zplot::PriceRange(bars, 0.0f, 399.0f, 0.05f, pmin, pmax);
    std::printf("  PriceRange      ok=%s y=[%.2f, %.2f]\n",
        got ? "true" : "false", pmin, pmax);

    // An empty slice reports failure instead of a bogus range.
    const bool none = zplot::PriceRange(bars, 1000.0f, 1100.0f, 0.05f, pmin, pmax);
    std::printf("  PriceRange      ok=%s  (empty slice)\n", none ? "true" : "false");

    // ---- 3. viewport fitting -------------------------------------------
    std::printf("\nFitViewport\n");
    const zplot::Viewport fitAll = zplot::FitViewport(bars, 0, 0, 800, 500, 0);
    std::printf("  all bars : x=[%.0f,%.0f] y=[%.2f,%.2f]\n",
        fitAll.xMin, fitAll.xMax, fitAll.yMin, fitAll.yMax);

    const zplot::Viewport fit100 = zplot::FitViewport(bars, 0, 0, 800, 500, 100);
    std::printf("  last 100 : x=[%.0f,%.0f] y=[%.2f,%.2f]\n",
        fit100.xMin, fit100.xMax, fit100.yMin, fit100.yMax);

    // FitViewport on an empty vector must stay safe.
    const std::vector<zplot::Bar> empty;
    const zplot::Viewport fitEmpty = zplot::FitViewport(empty, 0, 0, 800, 500, 0);
    std::printf("  empty    : x=[%.0f,%.0f] y=[%.2f,%.2f]  (no crash)\n",
        fitEmpty.xMin, fitEmpty.xMax, fitEmpty.yMin, fitEmpty.yMax);

    // ---- 4. the five chart styles --------------------------------------
    std::printf("\nchart styles\n");
    zplot::KLineStyle style;   // red up / cyan down by default
    const zplot::Viewport vp = zplot::FitViewport(bars, 0, 0, 800, 500, 200);

    struct { const char* label; void (*fn)(std::vector<zplot::PlotVertex>&,
                                            const std::vector<zplot::Bar>&,
                                            const zplot::Viewport&,
                                            const zplot::KLineStyle&); } styles[] = {
        { "AppendKLine",     &zplot::AppendKLine },
        { "AppendBamboo",    &zplot::AppendBamboo },
        { "AppendCloseLine", &zplot::AppendCloseLine },
        { "AppendTower",     &zplot::AppendTower },
    };
    for (const auto& s : styles) {
        std::vector<zplot::PlotVertex> vs;
        s.fn(vs, bars, vp, style);
        std::printf("  %-16s %zu verts (%zu tris)\n", s.label, vs.size(), vs.size() / 3);
    }

    // A custom palette: green up / red down (the western convention).
    zplot::KLineStyle western;
    western.up = zplot::Color4f::FromHex(0x1D9E75);
    western.down = zplot::Color4f::FromHex(0xE24B4A);
    western.hollowUp = false;
    std::vector<zplot::PlotVertex> wv;
    zplot::AppendKLine(wv, bars, vp, western);
    std::printf("  custom palette   %zu verts (solid bodies)\n", wv.size());

    return 0;
}
