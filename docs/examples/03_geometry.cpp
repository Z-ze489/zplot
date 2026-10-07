// =====================================================================
//  03_geometry.cpp
//  Sample for "Geometry" in docs/development.md
//
//  The geometry helpers are public API too: you can draw your own
//  overlays (levels, markers, boxes) with them without touching the
//  indicator code. This sample drives every helper directly.
// =====================================================================

#include <ZePlotGeometry.h>
#include <ZePlotTypes.h>
#include <color4f.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static void Line(const char* label, std::size_t before,
                 std::vector<zplot::PlotVertex>& v) {
    std::printf("  %-24s +%-6zu -> %zu verts (%zu tris)\n",
        label, v.size() - before, v.size(), v.size() / 3);
}

int main() {
    std::vector<zplot::PlotVertex> v;

    std::printf("viewport mapping\n");
    zplot::Viewport vp;
    vp.left = 0.0f;   vp.top = 0.0f;
    vp.width = 800.0f; vp.height = 400.0f;
    vp.xMin = 0.0f;   vp.xMax = 100.0f;
    vp.yMin = 7000.0f; vp.yMax = 8000.0f;

    std::printf("  x(0)=%.2f  x(50)=%.2f  x(100)=%.2f\n",
        vp.x(0.0f), vp.x(50.0f), vp.x(100.0f));
    std::printf("  y(8000)=%.2f  y(7000)=%.2f   <- y is flipped\n",
        vp.y(8000.0f), vp.y(7000.0f));
    std::printf("  dx()=%.3f  dataWidth()=%.1f\n", vp.dx(), vp.dataWidth());

    // A degenerate range must not produce NaN.
    zplot::Viewport flat;
    flat.left = 10.0f; flat.top = 20.0f; flat.width = 100.0f; flat.height = 50.0f;
    flat.xMin = flat.xMax = 5.0f;
    flat.yMin = flat.yMax = 5.0f;
    std::printf("  degenerate: x(5)=%.1f y(5)=%.1f dx=%.1f  (no NaN)\n",
        flat.x(5.0f), flat.y(5.0f), flat.dx());

    const zplot::Color4f c = zplot::Color4f::FromHex(0xE24B4A);

    std::printf("\nprimitives\n");
    std::size_t n = v.size(); zplot::PushTriangle(v, 0, 0, 100, 0, 50, 80, c);      Line("PushTriangle", n, v);
    n = v.size(); zplot::PushQuadPoints(v, 0, 0, 90, 0, 90, 40, 0, 40, c);         Line("PushQuadPoints", n, v);
    n = v.size(); zplot::PushRect(v, 0, 0, 60, 30, c);                              Line("PushRect", n, v);
    n = v.size(); zplot::PushSegment(v, 0, 0, 120, 90, 3.0f, c);                    Line("PushSegment", n, v);
    n = v.size(); zplot::PushPoint(v, 40, 40, 6.0f, c);                             Line("PushPoint", n, v);
    n = v.size(); zplot::PushBar(v, 0, 10, 100, 40, c, 1.0f);                       Line("PushBar", n, v);

    // A zero-length segment is dropped rather than producing degenerate tris.
    n = v.size(); zplot::PushSegment(v, 5, 5, 5, 5, 3.0f, c);                       Line("PushSegment(degenerate)", n, v);

    std::printf("\npolyline\n");
    const float xy[] = { 0, 0,  20, 40,  40, 10,  60, 50 };
    n = v.size(); zplot::PushPolyline(v, xy, 4, 2.0f, c);                            Line("PushPolyline", n, v);

    // NaN splits the run: the vertices between breaks are not connected.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float xy2[] = { 0, 0, 10, 10, nan, nan, 20, 20, 30, 30 };
    n = v.size(); zplot::PushPolyline(v, xy2, 5, 2.0f, c);                           Line("PushPolyline(NaN break)", n, v);

    // The callback form: generate points without materialising them.
    n = v.size();
    zplot::PushPolylineBy(v, 20, [](std::size_t i, float& x, float& y) {
        x = static_cast<float>(i) * 4.0f;
        y = 50.0f + 30.0f * std::sin(static_cast<float>(i) * 0.5f);
        return true;   // return false to break the run
    }, 1.5f, c);
    Line("PushPolylineBy", n, v);

    std::printf("\nseries -> vertices\n");
    zplot::SeriesPlot plot;
    plot.xMin = 0.0f; plot.xMax = 10.0f;
    plot.yMin = 0.0f; plot.yMax = 100.0f;
    plot.valid = true;

    zplot::Series line;
    line.prim = zplot::SeriesPrim::Line;
    line.color = c;
    line.width = 2.0f;
    for (int i = 0; i <= 10; ++i) { line.xy.push_back(float(i)); line.xy.push_back(float(i * 10)); }
    plot.series.push_back(line);

    zplot::Series dots;
    dots.prim = zplot::SeriesPrim::Dot;
    dots.color = zplot::Color4f::FromHex(0x1D9E75);
    dots.width = 5.0f;
    for (int i = 0; i <= 10; i += 2) { dots.xy.push_back(float(i)); dots.xy.push_back(float(100 - i * 10)); }
    plot.series.push_back(dots);

    zplot::Series bars;
    bars.prim = zplot::SeriesPrim::Bar;
    bars.color = zplot::Color4f::FromHex(0x378ADD);
    bars.barWidth = 6.0f;
    bars.barBase = 50.0f;
    for (int i = 0; i <= 10; ++i) { bars.xy.push_back(float(i)); bars.xy.push_back(50.0f + (i % 3) * 15.0f); }
    plot.series.push_back(bars);

    zplot::Viewport pv;
    pv.left = 0.0f; pv.top = 0.0f; pv.width = 400.0f; pv.height = 200.0f;
    pv.xMin = plot.xMin; pv.xMax = plot.xMax;
    pv.yMin = plot.yMin; pv.yMax = plot.yMax;

    n = v.size();
    zplot::AppendSeries(v, plot, pv);
    Line("AppendSeries(3 series)", n, v);

    std::printf("\nrange helpers\n");
    std::printf("  before PadRange: y=[%.1f,%.1f]\n", plot.yMin, plot.yMax);
    plot.PadRange(0.05f);
    std::printf("  after  PadRange: y=[%.1f,%.1f]\n", plot.yMin, plot.yMax);

    std::printf("\ntotal vertices: %zu  (%.1f KB)\n",
        v.size(), v.size() * sizeof(zplot::PlotVertex) / 1024.0);
    return 0;
}
