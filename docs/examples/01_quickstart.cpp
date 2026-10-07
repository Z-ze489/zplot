// =====================================================================
//  01_quickstart.cpp
//  Sample for "Quick start" in docs/development.md
//
//  Shows the two-stage API: Compute* -> data-space series + value range,
//  then geometry helpers -> pixels -> triangles.
//
//  Builds and runs with no window and no GPU: it only produces vertices.
// =====================================================================

#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>

#include <cstdio>
#include <vector>

// Replace with your own loader (CSV, socket, exchange callback).
static std::vector<zplot::Bar> LoadYourBars();

int main() {
    std::vector<zplot::Bar> bars = LoadYourBars();

    // 1. compute once -- data-space series plus the value range
    zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
    if (!macd.valid) {
        std::printf("MACD could not be computed\n");
        return 1;
    }

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

    std::printf("vertices           : %zu\n", verts.size());
    std::printf("triangles          : %zu\n", verts.size() / 3);
    std::printf("bytes to upload    : %zu\n", verts.size() * sizeof(zplot::PlotVertex));
    std::printf("vertex layout      : %zu floats (%zu bytes)\n",
        sizeof(zplot::PlotVertex) / sizeof(float), sizeof(zplot::PlotVertex));
    std::printf("series in the plot : %zu\n", macd.series.size());
    return 0;
}

// ---------------------------------------------------------------------
// A deterministic synthetic loader so this sample compiles and runs on
// its own. Swap it for your real data source.
// ---------------------------------------------------------------------
static std::vector<zplot::Bar> LoadYourBars() {
    std::vector<zplot::Bar> bars;
    bars.reserve(8000);

    double price = 7600.0;
    for (int i = 0; i < 8000; ++i) {
        // A fixed, reproducible walk -- no RNG, so the output is stable.
        const double wave = 40.0 * ((i % 120) - 60) / 60.0;
        const double drift = 0.02 * i;
        const double base = price + wave + drift;

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
