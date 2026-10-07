// =====================================================================
//  05_backend.cpp
//  Sample for "Wiring the vertices into your own renderer" in
//  docs/development.md
//
//  zplot emits a flat vertex array and nothing else. This sample is a
//  fake backend: it takes the vertices and does the three things every
//  real backend must do -- interpret the layout, build a buffer, and
//  hand it to a draw call. No GL/D3D/Vulkan is linked here on purpose.
// =====================================================================

#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>
#include <ZePlotTypes.h>
#include <ZeTrendIndicator.h>
#include <ZeVolumeIndicator.h>

#include <cstdio>
#include <cstddef>
#include <vector>

// ---- what a real backend would hold ---------------------------------
struct GpuBuffer {
    unsigned vao = 0, vbo = 0;
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
};

// Stands in for glBufferData / D3D11 UpdateSubresource / vkCmdCopyBuffer.
static GpuBuffer Upload(const std::vector<zplot::PlotVertex>& verts) {
    GpuBuffer b;
    b.vertexCount = verts.size();
    b.triangleCount = verts.size() / 3;
    // A real backend would do:
    //   glGenVertexArrays(1, &b.vao);
    //   glGenBuffers(1, &b.vbo);
    //   glBufferData(GL_ARRAY_BUFFER,
    //                verts.size() * sizeof(zplot::PlotVertex),
    //                verts.data(), GL_STATIC_DRAW);
    //   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
    //                         sizeof(zplot::PlotVertex), (void*)0);              // xy
    //   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
    //                         sizeof(zplot::PlotVertex), (void*)(2*sizeof(float))); // rgba
    return b;
}

static void Draw(const GpuBuffer& b, const char* label) {
    std::printf("  %-22s drawArrays(GL_TRIANGLES, 0, %zu)   // %zu tris\n",
        label, b.vertexCount, b.triangleCount);
}

int main() {
    // ---- vertex layout contract ----------------------------------------
    std::printf("vertex layout\n");
    std::printf("  sizeof(PlotVertex) = %zu bytes = %zu floats\n",
        sizeof(zplot::PlotVertex), sizeof(zplot::PlotVertex) / sizeof(float));
    std::printf("  offsets: x=%zu y=%zu r=%zu g=%zu b=%zu a=%zu\n",
        offsetof(zplot::PlotVertex, x), offsetof(zplot::PlotVertex, y),
        offsetof(zplot::PlotVertex, r), offsetof(zplot::PlotVertex, g),
        offsetof(zplot::PlotVertex, b), offsetof(zplot::PlotVertex, a));
    std::printf("  size check passes at compile time via static_assert in ZePlotTypes.h\n");

    // ---- sample data ----------------------------------------------------
    std::vector<zplot::Bar> bars;
    const int N = 300;
    bars.reserve(N);
    for (int i = 0; i < N; ++i) {
        const double base = 7600.0 + 40.0 * ((i % 120) - 60) / 60.0 + 0.02 * i;
        zplot::Bar b;
        b.index = i; b.time = "2026-10-07 09:00";
        b.open = base; b.high = base + 12.0 + (i % 7);
        b.low = base - 12.0 - (i % 5); b.close = base + ((i % 3) - 1) * 4.0;
        b.volume = 1000.0 + (i % 500); b.openInterest = 50000.0 + (i % 900);
        b.settle = b.close;
        bars.push_back(b);
    }

    // ---- a three-panel layout, all sharing one x range -----------------
    const float W = 1280.0f, H = 720.0f;
    const float pricePanelH = 420.0f;
    const float volPanelH   = 120.0f;
    const float oscPanelH   = 140.0f;

    // Price panel: candles + a MA overlay, same viewport.
    const zplot::Viewport priceVp =
        zplot::FitViewport(bars, 0.0f, 0.0f, W, pricePanelH, 200);

    zplot::KLineStyle kstyle;
    std::vector<zplot::PlotVertex> priceVerts;
    zplot::AppendKLine(priceVerts, bars, priceVp, kstyle);

    zplot::SeriesPlot ma = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
    zplot::Viewport maVp = priceVp;               // MUST share the x range
    maVp.yMin = ma.yMin; maVp.yMax = ma.yMax;
    zplot::AppendSeries(priceVerts, ma, maVp);

    // Volume panel, directly below.
    //
    // The x range is taken from the PRICE panel, not from the volume plot.
    // Each plot carries the range of the data it was computed over, and the
    // volume series spans every bar, while the price panel only shows the
    // last 200. Copying the price xMin/xMax is what keeps the two panels
    // locked to the same bars. Copying vol.xMin/vol.xMax instead would
    // silently shift the volume bars out of alignment.
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

    std::printf("\npanels\n");
    Draw(Upload(priceVerts), "price (KLine + MA)");

    std::vector<zplot::PlotVertex> volVerts;
    zplot::AppendSeries(volVerts, vol, volVp);
    Draw(Upload(volVerts), "volume (CJL)");

    // Oscillator panel at the bottom.
    zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
    zplot::Viewport oscVp;
    oscVp.left = 0.0f;
    oscVp.top = pricePanelH + volPanelH + 16.0f;
    oscVp.width = W;
    oscVp.height = oscPanelH;
    oscVp.xMin = priceVp.xMin;      // same rule here
    oscVp.xMax = priceVp.xMax;
    oscVp.yMin = macd.yMin;
    oscVp.yMax = macd.yMax;

    std::vector<zplot::PlotVertex> oscVerts;
    zplot::AppendSeries(oscVerts, macd, oscVp);
    Draw(Upload(oscVerts), "oscillator (MACD)");

    std::printf("  panels stack to y=%.0f  (canvas height %.0f)\n",
        oscVp.top + oscVp.height, H);

    // ---- how a real backend keeps panels aligned -----------------------
    std::printf("\nalignment rule\n");
    std::printf("  price panel x=[%.0f,%.0f]\n", priceVp.xMin, priceVp.xMax);
    std::printf("  MA overlay  x=[%.0f,%.0f]  <- copied from price\n", maVp.xMin, maVp.xMax);
    std::printf("  volume      x=[%.0f,%.0f]  <- copied from price (not from vol)\n",
        volVp.xMin, volVp.xMax);
    std::printf("  oscillator  x=[%.0f,%.0f]  <- copied from price\n", oscVp.xMin, oscVp.xMax);
    std::printf("  all four match: %s\n",
        (priceVp.xMin == maVp.xMin && priceVp.xMin == volVp.xMin &&
         priceVp.xMin == oscVp.xMin) ? "yes" : "NO -- panels would misalign");
    std::printf("  what vol.xMin/vol.xMax would have been: [%.0f,%.0f]\n",
        vol.xMin, vol.xMax);

    // ---- per-series buffers, if you want to toggle layers --------------
    std::printf("\nper-layer buffers\n");
    for (std::size_t i = 0; i < ma.series.size(); ++i) {
        std::vector<zplot::PlotVertex> sv;
        zplot::SeriesPlot one;
        one.series.push_back(ma.series[i]);
        one.xMin = ma.xMin; one.xMax = ma.xMax;
        one.yMin = ma.yMin; one.yMax = ma.yMax;
        zplot::AppendSeries(sv, one, maVp);
        char label[32];
        std::snprintf(label, sizeof(label), "MA series %zu", i);
        Draw(Upload(sv), label);
    }

    std::printf("\ntotal vertices across all buffers: %zu\n",
        priceVerts.size() + volVerts.size() + oscVerts.size());
    return 0;
}
