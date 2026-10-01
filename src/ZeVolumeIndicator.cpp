/**
 * @file ZeVolumeIndicator.cpp
 * @brief Volume and open-interest indicator implementations.
 */

#include <ZeVolumeIndicator.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace zplot {
namespace {

    // ===================================================================
    // ===================================================================

    constexpr float kLineWidth = 1.5f;

    constexpr float kBarWidth = 4.0f;

    constexpr Color4f Rgb(float r, float g, float b) { return Color4f(r, g, b, 1.0f); }

    constexpr Color4f kUpColor = Color4f(0.86f, 0.15f, 0.15f, 1.0f);

    constexpr Color4f kDownColor = Color4f(0.09f, 0.64f, 0.29f, 1.0f);

    std::vector<double> Volumes(const std::vector<Bar>& bars) {
        std::vector<double> out;
        out.reserve(bars.size());
        for (const Bar& b : bars) out.push_back(b.volume);
        return out;
    }

    std::vector<double> Closes(const std::vector<Bar>& bars) {
        std::vector<double> out;
        out.reserve(bars.size());
        for (const Bar& b : bars) out.push_back(b.close);
        return out;
    }

    std::vector<double> Interests(const std::vector<Bar>& bars) {
        std::vector<double> out;
        out.reserve(bars.size());
        for (const Bar& b : bars) out.push_back(b.openInterest);
        return out;
    }

    Series LineSeries(const std::vector<double>& values, std::size_t startBar,
        const std::vector<Bar>& bars, const Color4f& color) {
        Series s;
        s.prim = SeriesPrim::Line;
        s.width = kLineWidth;
        s.color = color;
        s.xy.reserve(values.size() * 2);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::size_t k = startBar + i;
            if (k >= bars.size()) break;
            s.xy.push_back(static_cast<float>(bars[k].index));
            s.xy.push_back(static_cast<float>(values[i]));
        }
        return s;
    }

    Series SegmentSeries(float x0, float y0, float x1, float y1, const Color4f& color) {
        Series s;
        s.prim = SeriesPrim::Line;
        s.width = kLineWidth;
        s.color = color;
        s.xy = { x0, y0, x1, y1 };
        return s;
    }

    Series BarSeries(const std::vector<double>& values, std::size_t startBar,
        const std::vector<Bar>& bars, const Color4f& color) {
        Series s;
        s.prim = SeriesPrim::Bar;
        s.color = color;
        s.barBase = 0.0f;
        s.barWidth = kBarWidth;
        s.xy.reserve(values.size() * 2);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::size_t k = startBar + i;
            if (k >= bars.size()) break;
            s.xy.push_back(static_cast<float>(bars[k].index));
            s.xy.push_back(static_cast<float>(values[i]));
        }
        return s;
    }

    void PushUpDownBars(std::vector<Series>& out, const std::vector<Bar>& bars,
        const std::vector<double>& values,
        const Color4f& upColor, const Color4f& downColor) {

        Series up, down;
        up.prim = down.prim = SeriesPrim::Bar;
        up.color = upColor;
        down.color = downColor;
        up.barBase = down.barBase = 0.0f;
        up.barWidth = down.barWidth = kBarWidth;

        const std::size_t n = std::min(values.size(), bars.size());
        for (std::size_t i = 0; i < n; ++i) {
            Series& dst = (bars[i].close >= bars[i].open) ? up : down;
            dst.xy.push_back(static_cast<float>(bars[i].index));
            dst.xy.push_back(static_cast<float>(values[i]));
        }

        out.push_back(std::move(up));
        out.push_back(std::move(down));
    }

    void PushSignBars(std::vector<Series>& out, const std::vector<double>& values,
        std::size_t startBar, const std::vector<Bar>& bars,
        const Color4f& upColor, const Color4f& downColor) {

        Series up, down;
        up.prim = down.prim = SeriesPrim::Bar;
        up.color = upColor;
        down.color = downColor;
        up.barBase = down.barBase = 0.0f;
        up.barWidth = down.barWidth = kBarWidth;

        for (std::size_t i = 0; i < values.size(); ++i) {
            const std::size_t k = startBar + i;
            if (k >= bars.size()) break;
            Series& dst = (values[i] >= 0.0) ? up : down;
            dst.xy.push_back(static_cast<float>(bars[k].index));
            dst.xy.push_back(static_cast<float>(values[i]));
        }

        out.push_back(std::move(up));
        out.push_back(std::move(down));
    }

    void ExtendRangeF(const std::vector<double>& values, float& lo, float& hi) {
        for (double v : values) {
            const float f = static_cast<float>(v);
            if (f < lo) lo = f;
            if (f > hi) hi = f;
        }
    }

    float MaxOf(const std::vector<double>& values, float fallback) {
        float m = fallback;
        for (double v : values) {
            const float f = static_cast<float>(v);
            if (f > m) m = f;
        }
        return m;
    }

    float MinOf(const std::vector<double>& values, float fallback) {
        float m = fallback;
        for (double v : values) {
            const float f = static_cast<float>(v);
            if (f < m) m = f;
        }
        return m;
    }

    std::vector<double> SimpleMA(const std::vector<double>& src, int period) {
        std::vector<double> out;
        if (period <= 0 || src.size() < static_cast<std::size_t>(period)) return out;
        out.reserve(src.size() - static_cast<std::size_t>(period) + 1);
        double sum = 0.0;
        for (std::size_t i = 0; i < src.size(); ++i) {
            sum += src[i];
            if (static_cast<int>(i) >= period) sum -= src[i - static_cast<std::size_t>(period)];
            if (static_cast<int>(i) >= period - 1) out.push_back(sum / period);
        }
        return out;
    }

    std::vector<double> RecursiveSMA(const std::vector<double>& src, int period) {
        std::vector<double> out;
        if (src.empty() || period <= 0) return out;
        out.reserve(src.size());
        double prev = src[0];
        out.push_back(prev);
        for (std::size_t i = 1; i < src.size(); ++i) {
            prev = (src[i] + (period - 1) * prev) / period;
            out.push_back(prev);
        }
        return out;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeCJL(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (bars.empty()) return plot;

        const int shortPeriod = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 5;
        const int longPeriod = (params.size() >= 2u && params[1] > 0.0)
            ? static_cast<int>(params[1]) : 10;

        const std::vector<double> vol = Volumes(bars);

        PushUpDownBars(plot.series, bars, vol, kUpColor, kDownColor);

        const std::vector<double> maShort = SimpleMA(vol, shortPeriod);
        const std::vector<double> maLong = SimpleMA(vol, longPeriod);
        if (!maShort.empty())
            plot.series.push_back(LineSeries(maShort,
                static_cast<std::size_t>(shortPeriod - 1), bars, Rgb(1.0f, 1.0f, 1.0f)));
        if (!maLong.empty())
            plot.series.push_back(LineSeries(maLong,
                static_cast<std::size_t>(longPeriod - 1), bars, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMax = MaxOf(vol, 1.0f);
        yMax = std::max(yMax, MaxOf(maShort, 0.0f));
        yMax = std::max(yMax, MaxOf(maLong, 0.0f));
        float margin = yMax * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = 0.0f;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeMV(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (bars.empty()) return plot;

        const int fastPeriod = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 5;
        const int slowPeriod = (params.size() >= 2u && params[1] > 0.0)
            ? static_cast<int>(params[1]) : 20;

        const std::vector<double> vol = Volumes(bars);
        const std::vector<double> mv1 = RecursiveSMA(vol, fastPeriod);
        const std::vector<double> mv2 = RecursiveSMA(vol, slowPeriod);
        if (mv1.empty() || mv2.empty()) return plot;

        plot.series.push_back(LineSeries(mv1, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(mv2, 0, bars, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        const float yMax = std::max(MaxOf(mv1, 0.0f), MaxOf(mv2, 0.0f));
        float yMin = std::min(MinOf(mv1, yMax), MinOf(mv2, yMax));
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeCCL(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (bars.empty()) return plot;

        const int maPeriod = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 5;

        const std::vector<double> oi = Interests(bars);

        plot.series.push_back(BarSeries(oi, 0, bars, Rgb(0.35f, 0.45f, 0.60f)));

        const std::vector<double> ma = SimpleMA(oi, maPeriod);
        if (!ma.empty())
            plot.series.push_back(LineSeries(ma,
                static_cast<std::size_t>(maPeriod - 1), bars, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMax = MaxOf(oi, 1.0f);
        yMax = std::max(yMax, MaxOf(ma, 0.0f));
        float margin = yMax * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = 0.0f;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeOPI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        (void)params;
        if (bars.size() < 2) return plot;

        const std::vector<double> oi = Interests(bars);
        std::vector<double> delta(oi.size(), 0.0);
        for (std::size_t i = 1; i < oi.size(); ++i)
            delta[i] = oi[i] - oi[i - 1];

        PushSignBars(plot.series, delta, 1, bars, kUpColor, kDownColor);

        plot.xMin = static_cast<float>(bars[1].index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(delta, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeOBV(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (bars.size() < 2) return plot;

        const int maPeriod = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 30;

        std::vector<double> obv;
        obv.reserve(bars.size());
        obv.push_back(0.0);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double prev = obv.back();
            if (bars[i].close > bars[i - 1].close)      obv.push_back(prev + bars[i].volume);
            else if (bars[i].close < bars[i - 1].close) obv.push_back(prev - bars[i].volume);
            else                                        obv.push_back(prev);
        }

        plot.series.push_back(LineSeries(obv, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        const std::vector<double> ma = SimpleMA(obv, maPeriod);
        if (!ma.empty())
            plot.series.push_back(LineSeries(ma,
                static_cast<std::size_t>(maPeriod - 1), bars, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(obv, yMin, yMax);
        ExtendRangeF(ma, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeVR(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        const int period = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 26;
        if (period <= 0) return plot;
        if (bars.size() < static_cast<std::size_t>(period + 1)) return plot;

        std::vector<double> vr;
        vr.reserve(bars.size() - static_cast<std::size_t>(period));
        for (std::size_t i = static_cast<std::size_t>(period); i < bars.size(); ++i) {
            double strong = 0.0, weak = 0.0;
            for (std::size_t j = i - static_cast<std::size_t>(period) + 1; j <= i; ++j) {
                if (j > 0 && bars[j].close > bars[j - 1].close) strong += bars[j].volume;
                else                                            weak += bars[j].volume;
            }
            vr.push_back((weak > 1e-9) ? (strong / weak * 100.0) : 100.0);
        }
        if (vr.empty()) return plot;

        const int startBar = period;
        const float xLeft = static_cast<float>(bars[static_cast<std::size_t>(startBar)].index);
        const float xRight = static_cast<float>(bars.back().index);

        const Color4f gray = Rgb(0.4f, 0.4f, 0.4f);
        plot.series.push_back(SegmentSeries(xLeft, 350.0f, xRight, 350.0f, gray));
        plot.series.push_back(SegmentSeries(xLeft, 160.0f, xRight, 160.0f, gray));
        plot.series.push_back(SegmentSeries(xLeft, 100.0f, xRight, 100.0f, gray));
        plot.series.push_back(LineSeries(vr, static_cast<std::size_t>(startBar), bars,
            Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = xLeft;
        plot.xMax = xRight;

        float yMin = 0.0f, yMax = 350.0f;
        ExtendRangeF(vr, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = 0.0f;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeAD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        (void)params;
        if (bars.empty()) return plot;

        std::vector<double> ad(bars.size(), 0.0);
        double acc = 0.0;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const double range = bars[i].high - bars[i].low;
            const double mfm = (range > 1e-9)
                ? ((bars[i].close - bars[i].low) - (bars[i].high - bars[i].close)) / range
                : 0.0;
            acc += mfm * bars[i].volume;
            ad[i] = acc;
        }

        plot.series.push_back(LineSeries(ad, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(ad, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputePVT(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        (void)params;
        if (bars.size() < 2) return plot;

        std::vector<double> pvt(bars.size(), 0.0);
        double acc = 0.0;
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double prev = bars[i - 1].close;
            const double rate = (std::fabs(prev) > 1e-12)
                ? (bars[i].close - prev) / prev : 0.0;
            acc += rate * bars[i].volume;
            pvt[i] = acc;
        }

        plot.series.push_back(LineSeries(pvt, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(pvt, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeWAD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        (void)params;
        if (bars.empty()) return plot;

        std::vector<double> wad(bars.size(), 0.0);
        double acc = 0.0;
        for (std::size_t i = 0; i < bars.size(); ++i) {
            double step = 0.0;
            if (i > 0) {
                const double prevClose = bars[i - 1].close;
                const double c = bars[i].close;
                if (c > prevClose)
                    step = c - std::min(prevClose, bars[i].low);
                else if (c < prevClose)
                    step = c - std::max(prevClose, bars[i].high);
            }
            acc += step;
            wad[i] = acc;
        }

        plot.series.push_back(LineSeries(wad, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(wad, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeWVAD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        (void)params;
        if (bars.empty()) return plot;

        std::vector<double> wvad(bars.size(), 0.0);
        for (std::size_t i = 0; i < bars.size(); ++i) {
            const double range = bars[i].high - bars[i].low;
            wvad[i] = (range > 1e-9)
                ? (bars[i].close - bars[i].open) / range * bars[i].volume
                : 0.0;
        }

        PushSignBars(plot.series, wvad, 0, bars, kUpColor, kDownColor);

        const float xL = static_cast<float>(bars.front().index);
        const float xR = static_cast<float>(bars.back().index);
        plot.series.push_back(SegmentSeries(xL, 0.0f, xR, 0.0f, Rgb(0.4f, 0.4f, 0.4f)));

        plot.xMin = xL;
        plot.xMax = xR;

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(wvad, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeVOSC(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (bars.empty()) return plot;

        const int shortP = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 12;
        const int longP = (params.size() >= 2u && params[1] > 0.0)
            ? static_cast<int>(params[1]) : 26;
        if (shortP <= 0 || longP <= 0) return plot;

        const std::vector<double> vol = Volumes(bars);
        const std::vector<double> maS = SimpleMA(vol, shortP);
        const std::vector<double> maL = SimpleMA(vol, longP);
        if (maS.empty() || maL.empty()) return plot;

        const int firstS = shortP - 1;
        const int firstL = longP - 1;
        const int begin = std::max(firstS, firstL);
        const int n = static_cast<int>(bars.size());

        const double kNaN = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> osc(bars.size(), kNaN);
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool any = false;

        for (int i = begin; i < n; ++i) {
            const std::size_t js = static_cast<std::size_t>(i - firstS);
            const std::size_t jl = static_cast<std::size_t>(i - firstL);
            if (js >= maS.size() || jl >= maL.size()) break;
            if (std::fabs(maS[js]) < 1e-12) continue;
            const double v = (maS[js] - maL[jl]) / maS[js] * 100.0;
            osc[static_cast<std::size_t>(i)] = v;
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
            any = true;
        }
        if (!any) return plot;

        plot.series.push_back(LineSeries(osc, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float lo = static_cast<float>(yMin), hi = static_cast<float>(yMax);
        float margin = (hi - lo) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = lo - margin;
        plot.yMax = hi + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeVROC(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        const int N = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 12;
        if (N <= 0 || static_cast<int>(bars.size()) <= N) return plot;

        const double kNaN = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> vroc(bars.size(), kNaN);
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (int i = N; i < static_cast<int>(bars.size()); ++i) {
            const double base = bars[static_cast<std::size_t>(i - N)].volume;
            if (std::fabs(base) < 1e-12) continue;
            const double v = (bars[static_cast<std::size_t>(i)].volume - base) / base * 100.0;
            vroc[static_cast<std::size_t>(i)] = v;
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
        if (yMin > yMax) return plot;

        plot.series.push_back(LineSeries(vroc, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float lo = static_cast<float>(yMin), hi = static_cast<float>(yMax);
        float margin = (hi - lo) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = lo - margin;
        plot.yMax = hi + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeVRSI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        const int N = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 6;
        if (N <= 0 || bars.size() < 2) return plot;

        std::vector<double> up, ab;
        up.reserve(bars.size() - 1);
        ab.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double d = bars[i].volume - bars[i - 1].volume;
            up.push_back(d > 0.0 ? d : 0.0);
            ab.push_back(std::fabs(d));
        }

        const std::vector<double> su = RecursiveSMA(up, N);
        const std::vector<double> sa = RecursiveSMA(ab, N);
        if (su.empty() || sa.empty()) return plot;

        const double kNaN = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> vrsi(bars.size(), kNaN);
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (std::size_t j = 0; j < su.size() && j + 1 < bars.size(); ++j) {
            const double v = (sa[j] > 1e-12) ? (su[j] / sa[j] * 100.0) : 100.0;
            vrsi[j + 1] = v;
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
        if (yMin > yMax) return plot;

        plot.series.push_back(LineSeries(vrsi, 0, bars, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float lo = static_cast<float>(yMin), hi = static_cast<float>(yMax);
        float margin = (hi - lo) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = lo - margin;
        plot.yMax = hi + margin;

        plot.valid = true;
        return plot;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputePriceVolumeTrend(const std::vector<Bar>& bars,
        const std::vector<double>& params) {

        SeriesPlot plot;
        const int N = (params.size() >= 1u && params[0] > 0.0)
            ? static_cast<int>(params[0]) : 25;
        if (N <= 0) return plot;

        const std::vector<double> priceMa = SimpleMA(Closes(bars), N);
        const std::vector<double> volMa = SimpleMA(Volumes(bars), N);
        if (priceMa.empty() || volMa.empty()) return plot;

        const int first = N - 1;
        const int n = static_cast<int>(bars.size());
        const double kNaN = std::numeric_limits<double>::quiet_NaN();

        std::vector<double> priceSeries(bars.size(), kNaN);
        std::vector<double> volSeries(bars.size(), kNaN);
        std::vector<double> dtSeries(bars.size(), kNaN);
        std::vector<double> ktSeries(bars.size(), kNaN);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool any = false;

        for (int i = first; i < n; ++i) {
            const std::size_t j = static_cast<std::size_t>(i - first);
            if (j >= priceMa.size() || j >= volMa.size()) break;
            priceSeries[static_cast<std::size_t>(i)] = priceMa[j];
            volSeries[static_cast<std::size_t>(i)] = volMa[j];
            yMin = std::min(yMin, priceMa[j]);
            yMax = std::max(yMax, priceMa[j]);
            any = true;

            if (i <= first) continue;
            const std::size_t jp = static_cast<std::size_t>(i - 1 - first);
            if (jp >= priceMa.size() || jp >= volMa.size()) continue;

            const bool up = (priceMa[j] > priceMa[jp]) && (volMa[j] > volMa[jp]);
            const bool down = (priceMa[j] < priceMa[jp]) && (volMa[j] < volMa[jp]);
            if (up) dtSeries[static_cast<std::size_t>(i)] = priceMa[j];
            if (down) ktSeries[static_cast<std::size_t>(i)] = priceMa[j];
        }
        if (!any) return plot;

        plot.series.push_back(LineSeries(priceSeries, 0, bars, Rgb(1.0f, 0.85f, 0.0f)));
        plot.series.push_back(LineSeries(volSeries, 0, bars, Rgb(0.45f, 0.45f, 0.50f)));
        plot.series.push_back(LineSeries(dtSeries, 0, bars, Rgb(1.0f, 0.0f, 1.0f)));
        plot.series.push_back(LineSeries(ktSeries, 0, bars, Rgb(0.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(bars.front().index);
        plot.xMax = static_cast<float>(bars.back().index);

        float lo = static_cast<float>(yMin), hi = static_cast<float>(yMax);
        float margin = (hi - lo) * 0.1f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = lo - margin;
        plot.yMax = hi + margin;

        plot.valid = true;
        return plot;
    }

} // namespace

// ===================================================================
// ===================================================================

SeriesPlot ComputeVolume(const std::vector<Bar>& bars,
    const std::string& name,
    const std::vector<double>& params) {

    if (bars.empty()) return SeriesPlot{};

    if (name == "CJL") return ComputeCJL(bars, params);
    else if (name == "MV") return ComputeMV(bars, params);
    else if (name == "CCL") return ComputeCCL(bars, params);
    else if (name == "OPI") return ComputeOPI(bars, params);
    else if (name == "OBV") return ComputeOBV(bars, params);
    else if (name == "VR") return ComputeVR(bars, params);
    else if (name == "AD") return ComputeAD(bars, params);
    else if (name == "PVT") return ComputePVT(bars, params);
    else if (name == "WAD") return ComputeWAD(bars, params);
    else if (name == "WVAD") return ComputeWVAD(bars, params);
    else if (name == "VOSC") return ComputeVOSC(bars, params);
    else if (name == "VROC") return ComputeVROC(bars, params);
    else if (name == "VRSI") return ComputeVRSI(bars, params);
    else if (name == "价量运行趋势") return ComputePriceVolumeTrend(bars, params);

    return SeriesPlot{};
}

} // namespace zplot
