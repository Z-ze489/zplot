// =====================================================================
//  02_indicators.cpp
//  Sample for "Indicators" in docs/development.md
//
//  Shows how to enumerate / probe the indicator families and how to read
//  a SeriesPlot back. Every name in the tables of docs/indicators.md is
//  exercised here, so if a name stops resolving this sample fails to run.
// =====================================================================

#include <ZeBar.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>
#include <ZeTrendIndicator.h>
#include <ZeVolumeIndicator.h>

#include <cstdio>
#include <string>
#include <vector>

// Every name the library currently accepts, with its default parameters.
struct Spec {
    const char* name;
    std::vector<double> params;
};

// ---- the full catalogue -------------------------------------------------
// Parameter counts and orders are taken from the implementation, not from
// the vendor docs -- several indicators fail silently when given the wrong
// count, and a few ignore their parameters entirely.
static const std::vector<Spec>& OscillatorSpecs() {
    static const std::vector<Spec> v = {
        { "MACD",   { 12, 26, 9 } },   { "KDJ",    { 9, 3, 3 } },
        { "KD",     { 9, 3, 3 } },     { "ROC",    { 12, 6 } },
        { "RSI",    { 6, 12, 24 } },   { "SLOWKD", { 9, 3, 3, 3 } },
        { "WR",     { 10, 6 } },       { "BIAS",   { 6, 12, 24 } },
        { "CR",     { 26, 10, 20, 40, 60 } },
        { "ATR",    { 14 } },          { "DMI",    { 14, 6 } },
        { "CCI",    { 14 } },          { "PSY",    { 12, 6 } },
        { "MTM",    { 12, 6 } },       { "DDI",    { 13, 30, 10, 5 } },
        { "DMA",    { 10, 50, 10 } },  { "ADTM",   { 23, 8 } },
        { "ARBR",   { 26 } },          { "LON",    { 10, 20 } },
        { "SRDM",   { 10 } },          { "SHORT",  { 3, 3 } },
        { "MI",     { 12 } },          { "DPO",    { 20 } },
        { "ASl",    { } },             // takes NO parameters
    };
    return v;
}

static const std::vector<Spec>& TrendSpecs() {
    static const std::vector<Spec> v = {
        { "BOLL",  { 20, 2, 1 } },    // N, M, P  (P is a multiplier)
        { "MA",    { 5, 10, 20, 30 } },
        { "SAR",   { 4, 2, 20 } },    { "SAR1",  { 4, 2, 20 } },
        { "PUBU",  { 4, 6, 9, 13, 18, 24 } },   // needs >= 6, values unused
        { "SP",    { 10 } },          { "SMA",   { 5, 10, 20 } },
        { "EMA",   { 12, 26 } },      { "HCL",   { 10 } },
        { "MIKE",  { 10 } },          { "BBI",   { 3, 6, 12, 24 } },
        { "DKX",   { 10 } },          { "EMA2",  { 12, 26 } },
        { "BBlBOLL", { 20, 2 } },     { "CDP",   { 5 } },
        { "ENV",   { 12, 6 } },       { "TRMA",  { 5 } },
        { "TSMA",  { 10 } },
        // Four indicators whose API string is Chinese -- easy to miss.
        { "MA扩展",  { 120, 240 } },
        { "唐奇安",  { 20, 20 } },
        { "空",      { } },           // draws nothing; valid is still true
    };
    return v;
}

static const std::vector<Spec>& VolumeSpecs() {
    static const std::vector<Spec> v = {
        { "CJL",  { 5, 10 } },  { "MV",   { 5, 10 } },
        { "CCL",  { 5, 10 } },  { "OPI",  { 5 } },
        { "OBV",  { 20 } },     { "VR",   { 26 } },
        { "AD",   { 20 } },     { "PVT",  { 20 } },
        { "WAD",  { 20 } },     { "WVAD", { 24 } },
        { "VOSC", { 12, 26 } }, { "VROC", { 12 } },
        { "VRSI", { 6, 12, 24 } },
        { "价量运行趋势", { 5, 10 } },   // API string is Chinese
    };
    return v;
}

// A deterministic loader so this file has no external dependency.
static std::vector<zplot::Bar> LoadBars() {
    std::vector<zplot::Bar> bars;
    bars.reserve(400);
    for (int i = 0; i < 400; ++i) {
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

static const char* PrimName(zplot::SeriesPrim p) {
    switch (p) {
    case zplot::SeriesPrim::Line: return "Line";
    case zplot::SeriesPrim::Dot:  return "Dot";
    case zplot::SeriesPrim::Bar:  return "Bar";
    }
    return "?";
}

static void Report(const char* family, const char* name,
                   const zplot::SeriesPlot& p) {
    if (!p.valid) {
        std::printf("  %-8s %-9s INVALID\n", family, name);
        return;
    }
    std::printf("  %-8s %-9s series=%zu  x=[%.0f,%.0f]  y=[%.4g,%.4g]\n",
        family, name, p.series.size(), p.xMin, p.xMax, p.yMin, p.yMax);
}

int main() {
    const std::vector<zplot::Bar> bars = LoadBars();

    int total = 0, invalid = 0;

    std::printf("Oscillators\n");
    for (const Spec& s : OscillatorSpecs()) {
        const zplot::SeriesPlot p = zplot::ComputeOscillator(bars, s.name, s.params);
        Report("osc", s.name, p);
        ++total; if (!p.valid) ++invalid;
    }

    std::printf("Trend\n");
    for (const Spec& s : TrendSpecs()) {
        const zplot::SeriesPlot p = zplot::ComputeTrend(bars, s.name, s.params);
        Report("trend", s.name, p);
        ++total; if (!p.valid) ++invalid;
    }

    std::printf("Volume / OI\n");
    for (const Spec& s : VolumeSpecs()) {
        const zplot::SeriesPlot p = zplot::ComputeVolume(bars, s.name, s.params);
        Report("volume", s.name, p);
        ++total; if (!p.valid) ++invalid;
    }

    std::printf("\ntotal=%d  invalid=%d\n", total, invalid);

    // ---- reading one plot back in detail --------------------------------
    const zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
    std::printf("\nMACD breakdown\n");
    for (std::size_t i = 0; i < macd.series.size(); ++i) {
        const zplot::Series& s = macd.series[i];
        std::printf("  series %zu: prim=%-4s points=%-4zu width=%.1f barWidth=%.1f barBase=%.4g\n",
            i, PrimName(s.prim), s.count(), s.width, s.barWidth, s.barBase);
    }
    std::printf("  range: x=[%.0f,%.0f] y=[%.6g,%.6g]\n",
        macd.xMin, macd.xMax, macd.yMin, macd.yMax);

    // An unknown name is a quiet failure, not an exception.
    const zplot::SeriesPlot bogus = zplot::ComputeOscillator(bars, "NOT_A_NAME", { 1 });
    std::printf("\nunknown name -> valid=%s\n", bogus.valid ? "true" : "false");

    // ---- the parameter-count trap ---------------------------------------
    // Too few (or, for ASl, any) parameters make the call fail silently:
    // no exception, no warning, just valid == false.
    std::printf("\nparameter-count trap\n");
    struct Probe { const char* name; std::vector<double> params; const char* note; };
    const Probe probes[] = {
        { "SLOWKD", { 9, 3, 3 },    "3 params -> needs 4 (N, M1, M2, M3)" },
        { "SLOWKD", { 9, 3, 3, 3 }, "4 params -> ok" },
        { "PSY",    { 12 },         "1 param  -> needs 2 (period, maPeriod)" },
        { "PSY",    { 12, 6 },      "2 params -> ok" },
        { "BOLL",   { 20, 2 },      "2 params -> needs 3 (N, M, P)" },
        { "BOLL",   { 20, 2, 1 },   "3 params -> ok" },
        { "PUBU",   { 1, 2, 3 },    "3 params -> needs 6" },
        { "PUBU",   { 1, 2, 3, 4, 5, 6 }, "6 params -> ok" },
        { "ASl",    { 6 },          "any param -> must be empty" },
        { "ASl",    { },            "no params -> ok" },
    };
    for (const Probe& p : probes) {
        const bool isOsc = (std::string(p.name) == "SLOWKD" ||
                            std::string(p.name) == "PSY"    ||
                            std::string(p.name) == "ASl");
        const zplot::SeriesPlot r = isOsc
            ? zplot::ComputeOscillator(bars, p.name, p.params)
            : zplot::ComputeTrend(bars, p.name, p.params);
        std::printf("  %-8s n=%-2zu valid=%-5s  %s\n",
            p.name, p.params.size(), r.valid ? "true" : "false", p.note);
    }

    return invalid == 0 ? 0 : 1;
}
