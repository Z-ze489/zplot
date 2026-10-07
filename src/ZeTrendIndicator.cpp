// SPDX-License-Identifier: MIT
/**
 * @file ZeTrendIndicator.cpp
 * @brief Trend indicator implementations.
 */

#include <ZeTrendIndicator.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace zplot {

namespace {

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    constexpr Color4f kWhite(1.0f, 1.0f, 1.0f);
    constexpr Color4f kYellow(1.0f, 1.0f, 0.0f);
    constexpr Color4f kPurple(0.5f, 0.0f, 1.0f);
    constexpr Color4f kGreen(0.0f, 1.0f, 0.0f);
    constexpr Color4f kGray(0.5f, 0.5f, 0.5f);
    constexpr Color4f kCyan(0.0f, 0.7f, 1.0f);
    constexpr Color4f kOrange(1.0f, 0.5f, 0.0f);
    constexpr Color4f kMagenta(1.0f, 0.0f, 1.0f);
    constexpr Color4f kPadGray(0.8f, 0.8f, 0.8f);

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    using PointList = std::vector<float>;

    enum class PriceType {
        Close,
        Open,
        High,
        Low,
        Typical,
        Median,
        Weighted,
    };

    Series MakeLine(PointList xy, const Color4f& color) {
        Series s;
        s.xy = std::move(xy);
        s.color = color;
        s.prim = SeriesPrim::Line;
        s.width = 1.5f;
        return s;
    }

    Series MakeDots(PointList xy, const Color4f& color) {
        Series s;
        s.xy = std::move(xy);
        s.color = color;
        s.prim = SeriesPrim::Dot;
        s.width = 3.0f;
        return s;
    }

    std::vector<double> ClosesOf(const std::vector<Bar>& bars) {
        std::vector<double> closes;
        closes.reserve(bars.size());
        for (const Bar& b : bars) closes.push_back(b.close);
        return closes;
    }

    void AppendValid(const std::vector<double>& values, PointList& xy,
        double& yMin, double& yMax, bool& hasPoint) {

        for (std::size_t i = 0; i < values.size(); ++i) {
            if (std::isnan(values[i])) continue;
            const double y = values[i];
            xy.push_back(static_cast<float>(i));
            xy.push_back(static_cast<float>(y));
            yMin = std::min(yMin, y);
            yMax = std::max(yMax, y);
            hasPoint = true;
        }
    }

    void ApplyMargin(double& yMin, double& yMax, bool hasPoint) {
        if (!hasPoint) {
            yMin = 0.0;
            yMax = 1.0;
            return;
        }
        double margin = (yMax - yMin) * 0.1;
        if (margin == 0.0) margin = 1.0;
        yMin -= margin;
        yMax += margin;
    }

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    std::vector<double> SmaSeries(const std::vector<double>& v, int period) {
        std::vector<double> sma;
        if (period <= 0) return sma;

        const std::size_t p = static_cast<std::size_t>(period);
        if (v.size() < p) return sma;

        sma.assign(v.size(), std::numeric_limits<double>::quiet_NaN());

        double sum = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            sum += v[i];
            if (i >= p - 1) {
                if (i >= p) sum -= v[i - p];
                sma[i] = sum / period;
            }
        }
        return sma;
    }

    std::vector<double> EmaSeries(const std::vector<double>& v, int period) {
        std::vector<double> ema(v.size(), std::numeric_limits<double>::quiet_NaN());
        if (period <= 0) return ema;

        const std::size_t p = static_cast<std::size_t>(period);
        if (v.size() < p) return ema;

        const double multiplier = 2.0 / (period + 1);

        double sum = 0.0;
        for (std::size_t i = 0; i < p; ++i) sum += v[i];
        ema[p - 1] = sum / period;

        for (std::size_t i = p; i < v.size(); ++i) {
            ema[i] = (v[i] - ema[i - 1]) * multiplier + ema[i - 1];
        }
        return ema;
    }

    int FirstValid(const std::vector<double>& v) {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (!std::isnan(v[i])) return static_cast<int>(i);
        }
        return -1;
    }

    void SpliceInto(std::vector<double>& dst, int offset, const std::vector<double>& sub) {
        const int n = static_cast<int>(dst.size());
        for (std::size_t j = 0; j < sub.size(); ++j) {
            if (std::isnan(sub[j])) continue;
            const int idx = offset + static_cast<int>(j);
            if (idx < n) dst[static_cast<std::size_t>(idx)] = sub[j];
        }
    }

    template <class SmoothFn, class CombineFn>
    std::vector<double> TripleSmooth(const std::vector<double>& v, int period,
        SmoothFn smooth, CombineFn combine) {

        std::vector<double> out(v.size(), std::numeric_limits<double>::quiet_NaN());
        if (period <= 0) return out;
        if (v.size() < static_cast<std::size_t>(period)) return out;

        const std::vector<double> s1 = smooth(v, period);
        const int start1 = FirstValid(s1);
        if (start1 == -1) return out;

        const std::vector<double> s1Sub(s1.begin() + start1, s1.end());
        const std::vector<double> s2Sub = smooth(s1Sub, period);

        std::vector<double> s2(v.size(), std::numeric_limits<double>::quiet_NaN());
        SpliceInto(s2, start1, s2Sub);
        const int start2 = FirstValid(s2);
        if (start2 == -1) return out;

        const std::vector<double> s2Sub2(s2.begin() + start2, s2.end());
        const std::vector<double> s3Sub = smooth(s2Sub2, period);

        std::vector<double> s3(v.size(), std::numeric_limits<double>::quiet_NaN());
        SpliceInto(s3, start2, s3Sub);

        for (std::size_t i = 0; i < v.size(); ++i)
            out[i] = combine(s1[i], s2[i], s3[i]);
        return out;
    }

    std::vector<double> TripleSmaSeries(const std::vector<double>& v, int period) {
        return TripleSmooth(v, period, SmaSeries,
            [](double, double, double s3) { return s3; });
    }

    std::vector<double> TripleEmaSeries(const std::vector<double>& v, int period) {
        return TripleSmooth(v, period, EmaSeries,
            [](double e1, double e2, double e3) {
                if (std::isnan(e1) || std::isnan(e2) || std::isnan(e3))
                    return std::numeric_limits<double>::quiet_NaN();
                return 3.0 * e1 - 3.0 * e2 + e3;
            });
    }

    std::vector<double> ExtractPrice(const std::vector<Bar>& bars, PriceType type) {
        std::vector<double> prices;
        prices.reserve(bars.size());
        for (const Bar& b : bars) {
            switch (type) {
            case PriceType::Close:    prices.push_back(b.close); break;
            case PriceType::Open:     prices.push_back(b.open); break;
            case PriceType::High:     prices.push_back(b.high); break;
            case PriceType::Low:      prices.push_back(b.low); break;
            case PriceType::Typical:  prices.push_back((b.high + b.low + b.close) / 3.0); break;
            case PriceType::Median:   prices.push_back((b.high + b.low) / 2.0); break;
            case PriceType::Weighted: prices.push_back((b.high + b.low + b.close + b.close) / 4.0); break;
            }
        }
        return prices;
    }

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    bool ComputeBollinger(const std::vector<Bar>& bars, int N, int M, double P, PriceType pt,
        PointList& midLine, PointList& upperLine, PointList& lowerLine,
        double& yMin, double& yMax) {

        midLine.clear();
        upperLine.clear();
        lowerLine.clear();
        yMin = std::numeric_limits<double>::max();
        yMax = std::numeric_limits<double>::lowest();

        const int total = static_cast<int>(bars.size());
        if (N < 2 || M < 2 || N > total || M > total) return false;

        const std::vector<double> prices = ExtractPrice(bars, pt);
        const int n = static_cast<int>(prices.size());

        const auto meanAt = [&prices](int i, int period) {
            double sum = 0.0;
            for (int j = i - period + 1; j <= i; ++j)
                sum += prices[static_cast<std::size_t>(j)];
            return sum / period;
        };

        for (int i = N - 1; i < n; ++i) {
            const double mean = meanAt(i, N);
            midLine.push_back(static_cast<float>(i));
            midLine.push_back(static_cast<float>(mean));
            yMin = std::min(yMin, mean);
            yMax = std::max(yMax, mean);
        }

        const int firstBand = std::max(N, M) - 1;
        for (int i = firstBand; i < n; ++i) {
            const double meanN = meanAt(i, N);
            const double meanM = meanAt(i, M);
            double sqSum = 0.0;
            for (int j = i - M + 1; j <= i; ++j) {
                const double diff = prices[static_cast<std::size_t>(j)] - meanM;
                sqSum += diff * diff;
            }
            const double sd = std::sqrt(sqSum / M);

            const double up = meanN + P * sd;
            const double low = meanN - P * sd;
            const float x = static_cast<float>(i);

            upperLine.push_back(x); upperLine.push_back(static_cast<float>(up));
            lowerLine.push_back(x); lowerLine.push_back(static_cast<float>(low));

            yMin = std::min({ yMin, up, low });
            yMax = std::max({ yMax, up, low });
        }

        return !midLine.empty();
    }

    bool ComputeBOLL(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        if (params.size() < 3u) return false;

        const int N = static_cast<int>(params[0]);
        const int M = static_cast<int>(params[1]);
        const double P = params[2];
        const PriceType pt = (params.size() >= 4u)
            ? static_cast<PriceType>(static_cast<int>(params[3]))
            : PriceType::Close;

        PointList mid, up, low;
        double yMin = 0.0, yMax = 0.0;
        if (!ComputeBollinger(bars, N, M, P, pt, mid, up, low, yMin, yMax)) return false;

        plot.series.push_back(MakeLine(std::move(mid), kWhite));
        plot.series.push_back(MakeLine(std::move(up), kYellow));
        plot.series.push_back(MakeLine(std::move(low), kPurple));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    std::vector<int> PickPeriods(const std::vector<double>& params, std::size_t maxCount,
        const std::vector<int>& fallback) {

        std::vector<int> periods;
        for (std::size_t i = 0; i < params.size() && i < maxCount; ++i) {
            const int p = static_cast<int>(params[i]);
            if (p > 0) periods.push_back(p);
        }
        if (periods.empty()) periods = fallback;
        return periods;
    }

    template <class CalcFn>
    bool FillLineFamily(const std::vector<double>& values,
        const std::vector<int>& periods,
        const std::vector<Color4f>& colors,
        CalcFn calc, SeriesPlot& plot) {

        plot.series.resize(periods.size());

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        for (std::size_t p = 0; p < periods.size(); ++p) {
            const std::vector<double> v = calc(values, periods[p]);
            Series& s = plot.series[p];
            AppendValid(v, s.xy, yMin, yMax, hasPoint);
            s.prim = SeriesPrim::Line;
            s.width = 1.5f;
            s.color = (p < colors.size()) ? colors[p] : kPadGray;
        }

        if (!hasPoint) return false;

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeMA(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::size_t lineCount = params.size();
        if (lineCount == 0) return false;

        const std::vector<double> closes = ClosesOf(bars);
        const int barCount = static_cast<int>(closes.size());
        const Color4f palette[] = { kWhite, kYellow, kPurple, kGreen, kGray };

        plot.series.resize(lineCount);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        for (std::size_t i = 0; i < lineCount; ++i) {
            const int period = static_cast<int>(params[i]);
            if (period < 1 || period > barCount) continue;

            const std::vector<double> sma = SmaSeries(closes, period);
            AppendValid(sma, plot.series[i].xy, yMin, yMax, hasPoint);
        }

        for (std::size_t i = 0; i < lineCount; ++i) {
            plot.series[i].prim = SeriesPrim::Line;
            plot.series[i].width = 1.5f;
            plot.series[i].color = (i < 5) ? palette[i] : kPadGray;
        }

        if (!hasPoint) return false;

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeSMA(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::vector<int> periods = PickPeriods(params, 8, { 5, 10, 20, 40, 60 });
        const std::vector<Color4f> colors = {
            kWhite, kYellow, kPurple, kGreen, kGray, kCyan, kOrange, kMagenta
        };
        return FillLineFamily(ClosesOf(bars), periods, colors, SmaSeries, plot);
    }

    bool ComputeEMA(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::vector<int> periods = PickPeriods(params, 8, { 5, 10, 20, 40, 60 });
        const std::vector<Color4f> colors = {
            kWhite, kYellow, kPurple, kGreen, kGray, kCyan, kOrange, kMagenta
        };
        return FillLineFamily(ClosesOf(bars), periods, colors, EmaSeries, plot);
    }

    bool ComputeEMA2(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::vector<int> periods = PickPeriods(params, 5, { 5, 10, 20, 40, 60 });
        const std::vector<Color4f> colors = { kWhite, kYellow, kPurple, kGreen, kGray };
        return FillLineFamily(ClosesOf(bars), periods, colors, EmaSeries, plot);
    }

    bool ComputeTRMA(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::vector<int> periods = PickPeriods(params, 5, { 5, 10, 20, 40, 60 });
        const std::vector<Color4f> colors = { kWhite, kYellow, kPurple, kGreen, kGray };
        return FillLineFamily(ClosesOf(bars), periods, colors, TripleSmaSeries, plot);
    }

    bool ComputeTSMA(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        const std::vector<int> periods = PickPeriods(params, 5, { 5, 10, 20, 40, 60 });
        const std::vector<Color4f> colors = { kWhite, kYellow, kPurple, kGreen, kGray };
        return FillLineFamily(ClosesOf(bars), periods, colors, TripleEmaSeries, plot);
    }

    // -------------------------------------------------------------------
    //  SAR / PUBU / SP
    // -------------------------------------------------------------------

    bool ComputeSAR(const std::vector<Bar>& bars,
        double afStart, double afMax, double afStep,
        PointList& upLine, PointList& downLine,
        double& yMin, double& yMax) {

        upLine.clear();
        downLine.clear();
        yMin = std::numeric_limits<double>::max();
        yMax = std::numeric_limits<double>::lowest();

        if (bars.size() < 2) return false;

        bool isLong = bars[1].close > bars[0].close;
        double ep = isLong ? bars[1].high : bars[1].low;
        double sar = isLong ? bars[0].low : bars[0].high;
        double af = afStart;

        PointList segment;

        for (std::size_t i = 2; i < bars.size(); ++i) {
            const double todayHigh = bars[i].high;
            const double todayLow = bars[i].low;

            double todaySAR = sar + af * (ep - sar);

            if (isLong) {
                const double minLow = std::min(bars[i - 1].low, bars[i - 2].low);
                if (todaySAR > minLow) todaySAR = minLow;
            }
            else {
                const double maxHigh = std::max(bars[i - 1].high, bars[i - 2].high);
                if (todaySAR < maxHigh) todaySAR = maxHigh;
            }

            bool reversal = false;

            if (isLong && todayLow < todaySAR) {
                reversal = true;
                isLong = false;
                sar = ep;
                ep = todayLow;
                af = afStart;

                if (!segment.empty()) {
                    upLine.insert(upLine.end(), segment.begin(), segment.end());
                    segment.clear();
                }
                const float x = static_cast<float>(i);
                segment.push_back(x);
                segment.push_back(static_cast<float>(sar));
                yMin = std::min(yMin, sar);
                yMax = std::max(yMax, sar);
            }
            else if (!isLong && todayHigh > todaySAR) {
                reversal = true;
                isLong = true;
                sar = ep;
                ep = todayHigh;
                af = afStart;

                if (!segment.empty()) {
                    downLine.insert(downLine.end(), segment.begin(), segment.end());
                    segment.clear();
                }
                const float x = static_cast<float>(i);
                segment.push_back(x);
                segment.push_back(static_cast<float>(sar));
                yMin = std::min(yMin, sar);
                yMax = std::max(yMax, sar);
            }

            if (!reversal) {
                if (isLong) {
                    if (todayHigh > ep) {
                        ep = todayHigh;
                        af = std::min(af + afStep, afMax);
                    }
                }
                else {
                    if (todayLow < ep) {
                        ep = todayLow;
                        af = std::min(af + afStep, afMax);
                    }
                }

                const float x = static_cast<float>(i);
                segment.push_back(x);
                segment.push_back(static_cast<float>(todaySAR));
                yMin = std::min(yMin, todaySAR);
                yMax = std::max(yMax, todaySAR);
            }

            sar = todaySAR;
        }

        if (isLong) upLine.insert(upLine.end(), segment.begin(), segment.end());
        else downLine.insert(downLine.end(), segment.begin(), segment.end());

        if (upLine.empty() && downLine.empty()) {
            yMin = 0.0;
            yMax = 1.0;
        }
        else {
            ApplyMargin(yMin, yMax, true);
        }
        return true;
    }

    bool ComputeSARPlot(const std::vector<Bar>& bars, const std::vector<double>& params,
        SeriesPlot& plot) {

        PointList upLine, downLine;
        double yMin = 0.0, yMax = 0.0;
        if (!ComputeSAR(bars, params[0], params[1], params[2], upLine, downLine, yMin, yMax))
            return false;

        plot.series.push_back(MakeDots(std::move(upLine), Color4f(1.0f, 0.2f, 0.2f)));
        plot.series.push_back(MakeDots(std::move(downLine), Color4f(0.0f, 0.8f, 0.2f)));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    void ComputePUBU(const std::vector<Bar>& bars, SeriesPlot& plot) {
        static const int kPeriods[6] = { 4, 6, 9, 13, 18, 24 };
        static const Color4f kPalette[6] = {
            kWhite, kYellow, kPurple, kGreen, kGray, Color4f(0.0f, 0.5f, 1.0f)
        };

        const std::vector<double> closes = ClosesOf(bars);
        plot.series.resize(6);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        const double kNaN = std::numeric_limits<double>::quiet_NaN();

        for (std::size_t p = 0; p < 6; ++p) {
            const int m = kPeriods[p];
            const std::vector<double> e = EmaSeries(closes, m);
            const std::vector<double> s2 = SmaSeries(closes, m * 2);
            const std::vector<double> s4 = SmaSeries(closes, m * 4);

            std::vector<double> line(closes.size(), kNaN);
            for (std::size_t i = 0; i < closes.size(); ++i) {
                if (std::isnan(e[i]) || std::isnan(s2[i]) || std::isnan(s4[i])) continue;
                line[i] = (e[i] + s2[i] + s4[i]) / 3.0;
            }

            Series& s = plot.series[p];
            AppendValid(line, s.xy, yMin, yMax, hasPoint);
            s.prim = SeriesPrim::Line;
            s.width = 1.5f;
            s.color = kPalette[p];
        }

        ApplyMargin(yMin, yMax, hasPoint);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
    }

    bool ComputeSP(const std::vector<Bar>& bars, int period, SeriesPlot& plot) {
        (void)period;
        if (bars.empty()) return false;

        bool anySettle = false;
        for (const Bar& b : bars) {
            if (b.settle > 0.0) { anySettle = true; break; }
        }
        if (!anySettle) return false;

        PointList sp;
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (std::size_t i = 0; i < bars.size(); ++i) {
            const double v = bars[i].settle;
            if (!(v > 0.0)) continue;
            sp.push_back(static_cast<float>(i));
            sp.push_back(static_cast<float>(v));
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }

        if (sp.empty()) return false;

        ApplyMargin(yMin, yMax, true);
        plot.series.push_back(MakeLine(std::move(sp), kWhite));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    // -------------------------------------------------------------------
    //  HCL / MIKE / BBI / DKX
    // -------------------------------------------------------------------

    bool ComputeHCL(const std::vector<Bar>& bars, int period, SeriesPlot& plot) {
        std::vector<double> closes, highs, lows;
        closes.reserve(bars.size());
        highs.reserve(bars.size());
        lows.reserve(bars.size());
        for (const Bar& b : bars) {
            closes.push_back(b.close);
            highs.push_back(b.high);
            lows.push_back(b.low);
        }

        const std::vector<double> maHigh = SmaSeries(highs, period);
        const std::vector<double> maClose = SmaSeries(closes, period);
        const std::vector<double> maLow = SmaSeries(lows, period);

        plot.series.resize(3);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        AppendValid(maHigh, plot.series[0].xy, yMin, yMax, hasPoint);
        AppendValid(maClose, plot.series[1].xy, yMin, yMax, hasPoint);
        AppendValid(maLow, plot.series[2].xy, yMin, yMax, hasPoint);

        if (!hasPoint) return false;

        const Color4f palette[3] = { kWhite, kPurple, kYellow };
        for (std::size_t i = 0; i < 3; ++i) {
            plot.series[i].prim = SeriesPrim::Line;
            plot.series[i].width = 1.5f;
            plot.series[i].color = palette[i];
        }

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeMIKE(const std::vector<Bar>& bars, int period, SeriesPlot& plot) {
        if (period < 1) return false;

        const int n = static_cast<int>(bars.size());
        plot.series.resize(6);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        for (int i = period - 1; i < n; ++i) {
            double hh = bars[static_cast<std::size_t>(i)].high;
            double ll = bars[static_cast<std::size_t>(i)].low;
            for (int j = i - period + 1; j <= i; ++j) {
                const Bar& b = bars[static_cast<std::size_t>(j)];
                if (b.high > hh) hh = b.high;
                if (b.low < ll) ll = b.low;
            }

            const Bar& cur = bars[static_cast<std::size_t>(i)];
            const double ty = (cur.high + cur.low + cur.close) / 3.0;

            //           WS:TYP-(HH-TYP)  MS:TYP-(HH-LL)  SS:2*LL-HH
            const double wr = ty + (ty - ll);
            const double mr = ty + (hh - ll);
            const double sr = 2.0 * hh - ll;
            const double ws = ty - (hh - ty);
            const double ms = ty - (hh - ll);
            const double ss = 2.0 * ll - hh;

            const float x = static_cast<float>(i);
            const double values[6] = { wr, mr, sr, ws, ms, ss };
            for (std::size_t k = 0; k < 6; ++k) {
                plot.series[k].xy.push_back(x);
                plot.series[k].xy.push_back(static_cast<float>(values[k]));
                yMin = std::min(yMin, values[k]);
                yMax = std::max(yMax, values[k]);
            }
            hasPoint = true;
        }

        if (!hasPoint) return false;

        const Color4f palette[6] = { kWhite, kYellow, kPurple, kGreen, kGray,
            Color4f(1.0f, 0.0f, 0.0f) };
        for (std::size_t i = 0; i < 6; ++i) {
            plot.series[i].prim = SeriesPrim::Line;
            plot.series[i].width = 1.5f;
            plot.series[i].color = palette[i];
        }

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeBBI(const std::vector<Bar>& bars, const std::vector<int>& periods,
        SeriesPlot& plot) {

        if (periods.size() != 4) return false;

        const std::vector<double> closes = ClosesOf(bars);
        const std::size_t n = closes.size();

        std::vector<std::vector<double>> smaList(4);
        for (std::size_t i = 0; i < 4; ++i)
            smaList[i] = SmaSeries(closes, periods[i]);

        for (std::size_t i = 0; i < 4; ++i)
            if (smaList[i].size() != n) return false;

        PointList bbi;
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (std::size_t idx = 0; idx < n; ++idx) {
            double sum = 0.0;
            int validCount = 0;
            for (std::size_t k = 0; k < 4; ++k) {
                if (!std::isnan(smaList[k][idx])) {
                    sum += smaList[k][idx];
                    ++validCount;
                }
            }
            if (validCount != 4) continue;

            const double val = sum / 4.0;
            bbi.push_back(static_cast<float>(idx));
            bbi.push_back(static_cast<float>(val));
            yMin = std::min(yMin, val);
            yMax = std::max(yMax, val);
        }

        if (bbi.empty()) return false;

        ApplyMargin(yMin, yMax, true);
        plot.series.push_back(MakeLine(std::move(bbi), kWhite));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeDKX(const std::vector<Bar>& bars, int period, SeriesPlot& plot) {
        if (period < 1) return false;

        const std::size_t n = bars.size();

        std::vector<double> medianPrices(n);
        for (std::size_t i = 0; i < n; ++i) {
            const Bar& b = bars[i];
            medianPrices[i] = (3.0 * b.close + b.high + b.low + b.open) / 6.0;
        }

        const double kNaN = std::numeric_limits<double>::quiet_NaN();

        std::vector<double> dkx(n, kNaN);
        const int kBack = 20;
        for (int i = kBack; i < static_cast<int>(n); ++i) {
            double sum = 0.0;
            for (int k = 0; k <= 18; ++k)
                sum += static_cast<double>(20 - k) *
                       medianPrices[static_cast<std::size_t>(i - k)];
            sum += medianPrices[static_cast<std::size_t>(i - 20)];
            dkx[static_cast<std::size_t>(i)] = sum / 210.0;
        }

        std::vector<double> madkx(n, kNaN);
        {
            const int firstB = kBack;
            double sum = 0.0;
            for (int i = firstB; i < static_cast<int>(n); ++i) {
                sum += dkx[static_cast<std::size_t>(i)];
                if (i >= firstB + period)
                    sum -= dkx[static_cast<std::size_t>(i - period)];
                if (i >= firstB + period - 1)
                    madkx[static_cast<std::size_t>(i)] = sum / period;
            }
        }

        plot.series.resize(2);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        AppendValid(dkx, plot.series[0].xy, yMin, yMax, hasPoint);
        AppendValid(madkx, plot.series[1].xy, yMin, yMax, hasPoint);

        if (!hasPoint) return false;

        plot.series[0].prim = SeriesPrim::Line;
        plot.series[0].width = 1.5f;
        plot.series[0].color = kWhite;

        plot.series[1].prim = SeriesPrim::Line;
        plot.series[1].width = 1.5f;
        plot.series[1].color = kYellow;

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    // -------------------------------------------------------------------
    // -------------------------------------------------------------------

    bool ComputeBBlBOLL(const std::vector<Bar>& bars, int N, double P, SeriesPlot& plot) {
        const int n = static_cast<int>(bars.size());
        if (N < 2 || n < 24) return false;

        const std::vector<double> closes = ClosesOf(bars);
        if (static_cast<int>(closes.size()) != n) return false;

        const std::vector<double> m3 = SmaSeries(closes, 3);
        const std::vector<double> m6 = SmaSeries(closes, 6);
        const std::vector<double> m12 = SmaSeries(closes, 12);
        const std::vector<double> m24 = SmaSeries(closes, 24);
        if (m24.size() != closes.size()) return false;

        const double kNaN = std::numeric_limits<double>::quiet_NaN();
        const int firstBbi = 23;

        std::vector<double> bbi(closes.size(), kNaN);
        for (int i = firstBbi; i < n; ++i)
            bbi[static_cast<std::size_t>(i)] = (m3[static_cast<std::size_t>(i)]
                + m6[static_cast<std::size_t>(i)]
                + m12[static_cast<std::size_t>(i)]
                + m24[static_cast<std::size_t>(i)]) / 4.0;

        PointList midLine, upperLine, lowerLine;
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (int i = firstBbi; i < n; ++i) {
            const double b = bbi[static_cast<std::size_t>(i)];
            midLine.push_back(static_cast<float>(i));
            midLine.push_back(static_cast<float>(b));
            yMin = std::min(yMin, b);
            yMax = std::max(yMax, b);
        }

        for (int i = firstBbi + N - 1; i < n; ++i) {
            double sum = 0.0;
            for (int j = i - N + 1; j <= i; ++j)
                sum += bbi[static_cast<std::size_t>(j)];
            const double mean = sum / N;

            double sqSum = 0.0;
            for (int j = i - N + 1; j <= i; ++j) {
                const double diff = bbi[static_cast<std::size_t>(j)] - mean;
                sqSum += diff * diff;
            }
            const double sd = std::sqrt(sqSum / N);

            const double b = bbi[static_cast<std::size_t>(i)];
            const double up = b + P * sd;
            const double low = b - P * sd;

            upperLine.push_back(static_cast<float>(i));
            upperLine.push_back(static_cast<float>(up));
            lowerLine.push_back(static_cast<float>(i));
            lowerLine.push_back(static_cast<float>(low));

            yMin = std::min({ yMin, up, low });
            yMax = std::max({ yMax, up, low });
        }

        if (midLine.empty()) return false;

        plot.series.push_back(MakeLine(std::move(midLine), kWhite));
        if (!upperLine.empty()) plot.series.push_back(MakeLine(std::move(upperLine), kYellow));
        if (!lowerLine.empty()) plot.series.push_back(MakeLine(std::move(lowerLine), kPurple));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeCDP(const std::vector<Bar>& bars, SeriesPlot& plot) {
        const std::size_t n = bars.size();
        if (n < 2) return false;

        plot.series.resize(5);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (std::size_t i = 1; i < n; ++i) {
            const double prevHigh = bars[i - 1].high;
            const double prevLow = bars[i - 1].low;
            const double prevClose = bars[i - 1].close;

            const double cdp = (prevHigh + prevLow + prevClose) / 3.0;
            const double r1 = 2.0 * cdp - prevLow;
            const double r2 = cdp + (prevHigh - prevLow);
            const double s1 = 2.0 * cdp - prevHigh;
            const double s2 = cdp - (prevHigh - prevLow);

            const float x = static_cast<float>(i);
            const double values[5] = { cdp, r1, r2, s1, s2 };
            for (std::size_t k = 0; k < 5; ++k) {
                plot.series[k].xy.push_back(x);
                plot.series[k].xy.push_back(static_cast<float>(values[k]));
                yMin = std::min(yMin, values[k]);
                yMax = std::max(yMax, values[k]);
            }
        }

        const Color4f palette[5] = { kWhite, kYellow, kPurple, kGreen, kGray };
        for (std::size_t i = 0; i < 5; ++i) {
            plot.series[i].prim = SeriesPrim::Dot;
            plot.series[i].width = 3.0f;
            plot.series[i].color = palette[i];
        }

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeDonchian(const std::vector<Bar>& bars, int N1, int N2, SeriesPlot& plot) {
        if (N1 < 1 || N2 < 1) return false;

        const int n = static_cast<int>(bars.size());
        plot.series.resize(2);

        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();
        bool hasPoint = false;

        for (int i = 0; i < n; ++i) {
            const float x = static_cast<float>(i);

            double highest = -std::numeric_limits<double>::max();
            for (int j = std::max(0, i - N1 + 1); j <= i; ++j)
                highest = std::max(highest, bars[static_cast<std::size_t>(j)].high);
            if (highest != -std::numeric_limits<double>::max()) {
                plot.series[0].xy.push_back(x);
                plot.series[0].xy.push_back(static_cast<float>(highest));
                yMin = std::min(yMin, highest);
                yMax = std::max(yMax, highest);
                hasPoint = true;
            }

            double lowest = std::numeric_limits<double>::max();
            for (int j = std::max(0, i - N2 + 1); j <= i; ++j)
                lowest = std::min(lowest, bars[static_cast<std::size_t>(j)].low);
            if (lowest != std::numeric_limits<double>::max()) {
                plot.series[1].xy.push_back(x);
                plot.series[1].xy.push_back(static_cast<float>(lowest));
                yMin = std::min(yMin, lowest);
                yMax = std::max(yMax, lowest);
                hasPoint = true;
            }
        }

        if (!hasPoint) return false;

        plot.series[0].prim = SeriesPrim::Line;
        plot.series[0].width = 1.5f;
        plot.series[0].color = kWhite;

        plot.series[1].prim = SeriesPrim::Line;
        plot.series[1].width = 1.5f;
        plot.series[1].color = kYellow;

        ApplyMargin(yMin, yMax, true);
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    bool ComputeENV(const std::vector<Bar>& bars, int N, double P, SeriesPlot& plot) {
        if (N < 2) return false;

        const std::vector<double> sma = SmaSeries(ClosesOf(bars), N);

        PointList upper, lower;
        double yMin = std::numeric_limits<double>::max();
        double yMax = std::numeric_limits<double>::lowest();

        for (std::size_t i = 0; i < sma.size(); ++i) {
            if (std::isnan(sma[i])) continue;
            const double up = sma[i] * (1.0 + P / 100.0);
            const double low = sma[i] * (1.0 - P / 100.0);
            const float x = static_cast<float>(i);

            upper.push_back(x);
            upper.push_back(static_cast<float>(up));
            lower.push_back(x);
            lower.push_back(static_cast<float>(low));

            yMin = std::min(yMin, low);
            yMax = std::max(yMax, up);
        }

        if (upper.empty()) return false;

        ApplyMargin(yMin, yMax, true);
        plot.series.push_back(MakeLine(std::move(upper), kWhite));
        plot.series.push_back(MakeLine(std::move(lower), kYellow));
        plot.yMin = static_cast<float>(yMin);
        plot.yMax = static_cast<float>(yMax);
        return true;
    }

    std::vector<int> BbiPeriods(const std::vector<double>& params) {
        std::vector<int> periods = { 3, 6, 12, 24 };
        if (params.empty()) return periods;

        periods.clear();
        for (std::size_t i = 0; i < params.size() && i < 4; ++i) {
            const int p = static_cast<int>(params[i]);
            if (p > 0) periods.push_back(p);
        }
        while (periods.size() < 4) periods.push_back(24);
        return periods;
    }

} // namespace

    SeriesPlot ComputeTrend(const std::vector<Bar>& bars,
        const std::string& name,
        const std::vector<double>& params) {

        SeriesPlot plot;

        if (!bars.empty()) {
            plot.xMin = static_cast<float>(bars.front().index);
            plot.xMax = static_cast<float>(bars.back().index);
        }

        if (name == "空") {
            plot.valid = true;
            return plot;
        }

        bool ok = false;

        if (name == "BOLL" && params.size() >= 3) {
            ok = ComputeBOLL(bars, params, plot);
        }
        else if (name == "MA") {
            ok = ComputeMA(bars, params, plot);
        }
        else if (name == "MA扩展") {
            std::vector<double> p = params;
            if (p.empty()) { p.push_back(120.0); p.push_back(240.0); }
            ok = ComputeSMA(bars, p, plot);
        }
        else if (name == "SAR" && params.size() >= 3) {
            ok = ComputeSARPlot(bars, params, plot);
        }
        else if (name == "SAR1" && params.size() >= 3) {
            ok = ComputeSARPlot(bars, params, plot);
        }
        else if (name == "PUBU" && params.size() >= 6) {
            ComputePUBU(bars, plot);
            ok = true;
        }
        else if (name == "SP") {
            int period = 20;
            if (!params.empty()) period = std::max(2, static_cast<int>(params[0]));
            ok = ComputeSP(bars, period, plot);
        }
        else if (name == "SMA") {
            ok = ComputeSMA(bars, params, plot);
        }
        else if (name == "EMA") {
            ok = ComputeEMA(bars, params, plot);
        }
        else if (name == "HCL") {
            int period = 10;
            if (!params.empty()) period = std::max(2, static_cast<int>(params[0]));
            ok = ComputeHCL(bars, period, plot);
        }
        else if (name == "MIKE") {
            int period = 12;
            if (!params.empty()) period = std::max(2, static_cast<int>(params[0]));
            ok = ComputeMIKE(bars, period, plot);
        }
        else if (name == "BBI") {
            ok = ComputeBBI(bars, BbiPeriods(params), plot);
        }
        else if (name == "DKX") {
            int period = 10;
            if (!params.empty()) period = std::max(2, static_cast<int>(params[0]));
            ok = ComputeDKX(bars, period, plot);
        }
        else if (name == "EMA2") {
            ok = ComputeEMA2(bars, params, plot);
        }
        else if (name == "BBlBOLL") {
            int N = 10;
            double P = 3.0;
            if (params.size() >= 2) {
                N = std::max(2, static_cast<int>(params[0]));
                P = params[1];
            }
            ok = ComputeBBlBOLL(bars, N, P, plot);
        }
        else if (name == "CDP") {
            ok = ComputeCDP(bars, plot);
        }
        else if (name == "唐奇安") {
            int N1 = 20, N2 = 20;
            if (params.size() >= 2) {
                N1 = std::max(1, static_cast<int>(params[0]));
                N2 = std::max(1, static_cast<int>(params[1]));
            }
            ok = ComputeDonchian(bars, N1, N2, plot);
        }
        else if (name == "ENV") {
            int N = 14;
            double P = 6.0;
            if (params.size() >= 2) {
                N = std::max(2, static_cast<int>(params[0]));
                P = params[1];
            }
            ok = ComputeENV(bars, N, P, plot);
        }
        else if (name == "TRMA") {
            ok = ComputeTRMA(bars, params, plot);
        }
        else if (name == "TSMA") {
            ok = ComputeTSMA(bars, params, plot);
        }
        else {
            return SeriesPlot{};
        }

        if (!ok) return SeriesPlot{};

        plot.valid = true;
        return plot;
    }

} // namespace zplot
