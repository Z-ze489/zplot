/**
 * @file ZeOscillatorIndicator.cpp
 * @brief Oscillator indicator implementations.
 */

#include <ZeOscillatorIndicator.h>

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

    std::vector<double> Closes(const std::vector<Bar>& bars) {
        std::vector<double> out;
        out.reserve(bars.size());
        for (const Bar& b : bars) out.push_back(b.close);
        return out;
    }

    Series LineSeries(const std::vector<double>& values, std::size_t begin, std::size_t count,
        int startIndex, const Color4f& color) {
        Series s;
        s.prim = SeriesPrim::Line;
        s.width = kLineWidth;
        s.color = color;
        s.xy.reserve(count * 2);
        for (std::size_t i = 0; i < count && begin + i < values.size(); ++i) {
            s.xy.push_back(static_cast<float>(startIndex + static_cast<int>(i)));
            s.xy.push_back(static_cast<float>(values[begin + i]));
        }
        return s;
    }

    Series LineSeries(const std::vector<double>& values, int startIndex, const Color4f& color) {
        return LineSeries(values, 0, values.size(), startIndex, color);
    }

    Series SegmentSeries(float x0, float y0, float x1, float y1, const Color4f& color) {
        Series s;
        s.prim = SeriesPrim::Line;
        s.width = kLineWidth;
        s.color = color;
        s.xy = { x0, y0, x1, y1 };
        return s;
    }

    void PushSignBars(std::vector<Series>& out, const std::vector<double>& values,
        std::size_t begin, std::size_t count, int startIndex,
        const Color4f& upColor, const Color4f& downColor) {

        Series up, down;
        up.prim = SeriesPrim::Bar;
        down.prim = SeriesPrim::Bar;
        up.color = upColor;
        down.color = downColor;
        up.barBase = 0.0f;
        down.barBase = 0.0f;
        up.barWidth = kBarWidth;
        down.barWidth = kBarWidth;

        for (std::size_t i = 0; i < count && begin + i < values.size(); ++i) {
            const double v = values[begin + i];
            Series& dst = (v >= 0.0) ? up : down;
            dst.xy.push_back(static_cast<float>(startIndex + static_cast<int>(i)));
            dst.xy.push_back(static_cast<float>(v));
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

    void ExtendRange(const std::vector<double>& values, double& lo, double& hi) {
        for (double v : values) {
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
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

    std::vector<double> RollingSum(const std::vector<double>& src, int period) {
        std::vector<double> out;
        if (period <= 0 || src.size() < static_cast<std::size_t>(period)) return out;
        out.reserve(src.size() - static_cast<std::size_t>(period) + 1);
        double sum = 0.0;
        for (std::size_t i = 0; i < src.size(); ++i) {
            sum += src[i];
            if (static_cast<int>(i) >= period) sum -= src[i - static_cast<std::size_t>(period)];
            if (static_cast<int>(i) >= period - 1) out.push_back(sum);
        }
        return out;
    }

    std::vector<double> RecursiveSmaW(const std::vector<double>& src, int period, int weight) {
        std::vector<double> out;
        if (src.empty() || period <= 0 || weight <= 0 || weight > period) return out;
        out.reserve(src.size());
        double prev = src[0];
        out.push_back(prev);
        const double w = static_cast<double>(weight);
        for (std::size_t i = 1; i < src.size(); ++i) {
            prev = (src[i] * w + prev * (period - w)) / period;
            out.push_back(prev);
        }
        return out;
    }

    std::vector<double> RecursiveSMA(const std::vector<double>& src, int period) {
        return RecursiveSmaW(src, period, 1);
    }

    std::vector<double> WilderSmooth(const std::vector<double>& src, int period) {
        std::vector<double> out;
        if (period <= 0 || src.size() < static_cast<std::size_t>(period)) return out;
        out.reserve(src.size() - static_cast<std::size_t>(period) + 1);
        double sum = 0.0;
        for (int i = 0; i < period; ++i) sum += src[static_cast<std::size_t>(i)];
        double avg = sum / period;
        out.push_back(avg);
        for (int i = period; i < static_cast<int>(src.size()); ++i) {
            avg = (avg * (period - 1) + src[static_cast<std::size_t>(i)]) / period;
            out.push_back(avg);
        }
        return out;
    }

    // ===================================================================
    // ===================================================================

    struct MACDData {
        std::vector<double> dif;
        std::vector<double> dea;
        std::vector<double> histogram;  ///< `(DIF - DEA) * 2`
    };

    MACDData MacdCalc(const std::vector<double>& closes, int fast, int slow, int signal) {
        MACDData result;
        if (fast <= 0 || slow <= 0 || signal <= 0) return result;
        if (closes.size() < static_cast<std::size_t>(slow)) return result;

        double initFast = 0.0, initSlow = 0.0;
        for (int i = 0; i < fast; ++i) initFast += closes[static_cast<std::size_t>(i)];
        for (int i = 0; i < slow; ++i) initSlow += closes[static_cast<std::size_t>(i)];
        double emaFast = initFast / fast;
        double emaSlow = initSlow / slow;

        const double alphaFast = 2.0 / (fast + 1);
        const double alphaSlow = 2.0 / (slow + 1);
        const double alphaSignal = 2.0 / (signal + 1);

        std::vector<double> dif;
        dif.reserve(closes.size());
        for (std::size_t i = 0; i < closes.size(); ++i) {
            emaFast = alphaFast * closes[i] + (1 - alphaFast) * emaFast;
            emaSlow = alphaSlow * closes[i] + (1 - alphaSlow) * emaSlow;
            dif.push_back(emaFast - emaSlow);
        }

        double initDEA = 0.0;
        const int seed = std::min(signal, static_cast<int>(dif.size()));
        for (int i = 0; i < seed; ++i) initDEA += dif[static_cast<std::size_t>(i)];
        double emaSignal = initDEA / seed;

        result.dif.reserve(dif.size());
        result.dea.reserve(dif.size());
        result.histogram.reserve(dif.size());
        for (std::size_t i = 0; i < dif.size(); ++i) {
            emaSignal = alphaSignal * dif[i] + (1 - alphaSignal) * emaSignal;
            result.dif.push_back(dif[i]);
            result.dea.push_back(emaSignal);
            result.histogram.push_back((dif[i] - emaSignal) * 2.0);
        }
        return result;
    }

    struct KDJData {
        std::vector<double> K;
        std::vector<double> D;
        std::vector<double> J;
    };

    KDJData KdjCalc(const std::vector<Bar>& bars, int N, int M1, int M2) {
        KDJData result;
        if (N <= 0 || M1 <= 0 || M2 <= 0) return result;
        if (bars.size() < static_cast<std::size_t>(N)) return result;

        const std::size_t total = bars.size() - static_cast<std::size_t>(N) + 1;
        result.K.reserve(total);
        result.D.reserve(total);
        result.J.reserve(total);

        double prevK = 50.0, prevD = 50.0;
        for (int i = N - 1; i < static_cast<int>(bars.size()); ++i) {
            const std::size_t ci = static_cast<std::size_t>(i);
            double highest = bars[ci].high;
            double lowest = bars[ci].low;
            for (int j = i - N + 1; j < i; ++j) {
                const std::size_t cj = static_cast<std::size_t>(j);
                highest = std::max(highest, bars[cj].high);
                lowest = std::min(lowest, bars[cj].low);
            }
            double rsv = 50.0;
            if (highest - lowest > 1e-9)
                rsv = (bars[ci].close - lowest) / (highest - lowest) * 100.0;

            const double k = (prevK * (M1 - 1) + rsv) / M1;
            const double d = (prevD * (M2 - 1) + k) / M2;
            result.K.push_back(k);
            result.D.push_back(d);
            result.J.push_back(3.0 * k - 2.0 * d);
            prevK = k;
            prevD = d;
        }
        return result;
    }

    std::vector<double> RocCalc(const std::vector<Bar>& bars, int period) {
        std::vector<double> roc;
        if (period <= 0) return roc;
        if (bars.size() <= static_cast<std::size_t>(period)) return roc;

        roc.reserve(bars.size() - static_cast<std::size_t>(period));
        for (int i = period; i < static_cast<int>(bars.size()); ++i) {
            const double prevClose = bars[static_cast<std::size_t>(i - period)].close;
            if (prevClose == 0.0) roc.push_back(0.0);
            else roc.push_back((bars[static_cast<std::size_t>(i)].close - prevClose) / prevClose * 100.0);
        }
        return roc;
    }

    std::vector<double> RsiCalc(const std::vector<double>& closes, int period) {
        std::vector<double> rsi;
        if (period <= 0) return rsi;
        if (closes.size() <= static_cast<std::size_t>(period)) return rsi;

        const auto toRsi = [](double gain, double loss) {
            return (loss == 0.0) ? 100.0 : 100.0 - 100.0 / (1.0 + gain / loss);
            };

        double avgGain = 0.0, avgLoss = 0.0;
        for (int i = 1; i <= period; ++i) {
            const double delta = closes[static_cast<std::size_t>(i)] - closes[static_cast<std::size_t>(i - 1)];
            if (delta > 0) avgGain += delta;
            else avgLoss -= delta;
        }
        avgGain /= period;
        avgLoss /= period;

        rsi.reserve(closes.size() - static_cast<std::size_t>(period));
        rsi.push_back(toRsi(avgGain, avgLoss));
        for (int i = period + 1; i < static_cast<int>(closes.size()); ++i) {
            const double delta = closes[static_cast<std::size_t>(i)] - closes[static_cast<std::size_t>(i - 1)];
            const double gain = (delta > 0) ? delta : 0.0;
            const double loss = (delta < 0) ? -delta : 0.0;
            avgGain = (avgGain * (period - 1) + gain) / period;
            avgLoss = (avgLoss * (period - 1) + loss) / period;
            rsi.push_back(toRsi(avgGain, avgLoss));
        }
        return rsi;
    }

    struct SlowKDData {
        std::vector<double> slowK;
        std::vector<double> slowD;
    };

    SlowKDData SlowKdCalc(const std::vector<Bar>& bars, int N, int M1, int M2, int M3) {
        SlowKDData result;
        if (N <= 0 || M1 <= 0 || M2 <= 0 || M3 <= 0) return result;
        if (bars.size() < static_cast<std::size_t>(N)) return result;

        std::vector<double> rsv;
        rsv.reserve(bars.size() - static_cast<std::size_t>(N) + 1);
        for (int i = N - 1; i < static_cast<int>(bars.size()); ++i) {
            const std::size_t ci = static_cast<std::size_t>(i);
            double highest = bars[ci].high;
            double lowest = bars[ci].low;
            for (int j = i - N + 1; j < i; ++j) {
                const std::size_t cj = static_cast<std::size_t>(j);
                highest = std::max(highest, bars[cj].high);
                lowest = std::min(lowest, bars[cj].low);
            }
            double val = 50.0;
            if (highest - lowest > 1e-9)
                val = (bars[ci].close - lowest) / (highest - lowest) * 100.0;
            rsv.push_back(val);
        }
        if (rsv.empty()) return result;

        const std::vector<double> fastK = RecursiveSMA(rsv, M1);
        if (fastK.empty()) return result;
        const std::vector<double> fastD = RecursiveSMA(fastK, M2);
        if (fastD.empty()) return result;

        result.slowK = fastD;
        result.slowD = RecursiveSMA(result.slowK, M3);
        return result;
    }

    std::vector<double> WrCalc(const std::vector<Bar>& bars, int period) {
        std::vector<double> wr;
        if (period <= 0) return wr;
        if (bars.size() < static_cast<std::size_t>(period)) return wr;

        wr.reserve(bars.size() - static_cast<std::size_t>(period) + 1);
        for (int i = period - 1; i < static_cast<int>(bars.size()); ++i) {
            const std::size_t ci = static_cast<std::size_t>(i);
            double highest = bars[ci].high;
            double lowest = bars[ci].low;
            for (int j = i - period + 1; j < i; ++j) {
                const std::size_t cj = static_cast<std::size_t>(j);
                if (bars[cj].high > highest) highest = bars[cj].high;
                if (bars[cj].low < lowest) lowest = bars[cj].low;
            }
            const double range = highest - lowest;
            if (range < 1e-9) wr.push_back(-50.0);
            else wr.push_back((highest - bars[ci].close) / range * (-100.0));
        }
        return wr;
    }

    std::vector<double> BiasCalc(const std::vector<double>& closes, int maPeriod) {
        std::vector<double> bias;
        if (maPeriod <= 0) return bias;
        if (closes.size() < static_cast<std::size_t>(maPeriod)) return bias;

        bias.reserve(closes.size() - static_cast<std::size_t>(maPeriod) + 1);
        double sum = 0.0;
        for (int i = 0; i < static_cast<int>(closes.size()); ++i) {
            sum += closes[static_cast<std::size_t>(i)];
            if (i >= maPeriod) sum -= closes[static_cast<std::size_t>(i - maPeriod)];
            if (i >= maPeriod - 1) {
                const double avg = sum / maPeriod;
                const double close = closes[static_cast<std::size_t>(i)];
                bias.push_back((close - avg) / avg * 100.0);
            }
        }
        return bias;
    }

    std::vector<double> CrCalc(const std::vector<Bar>& bars, int N) {
        std::vector<double> cr;
        if (N <= 0) return cr;
        if (bars.size() < static_cast<std::size_t>(N + 1)) return cr;

        std::vector<double> up, down;
        up.reserve(bars.size() - 1);
        down.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double prevMid = (bars[i - 1].high + bars[i - 1].low
                + bars[i - 1].close) / 3.0;
            up.push_back(std::max(0.0, bars[i].high - prevMid));
            down.push_back(std::max(0.0, prevMid - bars[i].low));
        }

        const std::vector<double> sumUp = RollingSum(up, N);
        const std::vector<double> sumDown = RollingSum(down, N);
        if (sumUp.empty()) return cr;

        cr.reserve(sumUp.size());
        for (std::size_t i = 0; i < sumUp.size(); ++i) {
            if (sumDown[i] == 0.0) {
                cr.push_back(cr.empty() ? 100.0 : cr.back());
            }
            else {
                cr.push_back(sumUp[i] / sumDown[i] * 100.0);
            }
        }
        return cr;
    }

    struct ATRData {
        std::vector<double> tr;
        std::vector<double> atr;
    };

    ATRData AtrCalc(const std::vector<Bar>& bars, int period) {
        ATRData result;
        if (period <= 0) return result;
        if (bars.size() < 2) return result;

        result.tr.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double high = bars[i].high;
            const double low = bars[i].low;
            const double prevClose = bars[i - 1].close;
            result.tr.push_back(std::max({ high - low, std::abs(high - prevClose),
                                           std::abs(low - prevClose) }));
        }
        result.atr = SimpleMA(result.tr, period);
        return result;
    }

    struct DMIData {
        std::vector<double> plusDI;   ///< +DI
        std::vector<double> minusDI;  ///< -DI
        std::vector<double> adx;
        std::vector<double> adxr;
    };

    DMIData DmiCalc(const std::vector<Bar>& bars, int diPeriod, int adxPeriod) {
        DMIData result;
        if (diPeriod <= 0 || adxPeriod <= 0) return result;
        if (bars.size() < static_cast<std::size_t>(diPeriod + 1)) return result;

        std::vector<double> tr, plusDM, minusDM;
        tr.reserve(bars.size() - 1);
        plusDM.reserve(bars.size() - 1);
        minusDM.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double high = bars[i].high;
            const double low = bars[i].low;
            const double prevH = bars[i - 1].high;
            const double prevL = bars[i - 1].low;
            const double prevC = bars[i - 1].close;

            tr.push_back(std::max({ high - low, std::abs(high - prevC),
                                    std::abs(low - prevC) }));

            const double upMove = high - prevH;
            const double downMove = prevL - low;
            plusDM.push_back((upMove > downMove && upMove > 0) ? upMove : 0.0);
            minusDM.push_back((downMove > upMove && downMove > 0) ? downMove : 0.0);
        }

        const std::vector<double> sumTR = RollingSum(tr, diPeriod);
        const std::vector<double> sumPDM = RollingSum(plusDM, diPeriod);
        const std::vector<double> sumMDM = RollingSum(minusDM, diPeriod);
        if (sumTR.empty()) return result;

        const int diCount = static_cast<int>(sumTR.size());
        const std::size_t count = static_cast<std::size_t>(diCount);
        std::vector<double> dx;
        dx.reserve(count);
        result.plusDI.reserve(count);
        result.minusDI.reserve(count);
        for (int i = 0; i < diCount; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            const double trSum = sumTR[k];
            const double pdi = (trSum == 0.0) ? 0.0 : sumPDM[k] / trSum * 100.0;
            const double mdi = (trSum == 0.0) ? 0.0 : sumMDM[k] / trSum * 100.0;
            result.plusDI.push_back(pdi);
            result.minusDI.push_back(mdi);

            const double sumDI = pdi + mdi;
            dx.push_back((sumDI == 0.0) ? 0.0 : std::abs(pdi - mdi) / sumDI * 100.0);
        }

        result.adx = SimpleMA(dx, adxPeriod);

        if (static_cast<int>(result.adx.size()) > adxPeriod) {
            const std::size_t back = static_cast<std::size_t>(adxPeriod);
            result.adxr.reserve(result.adx.size() - back);
            for (std::size_t i = back; i < result.adx.size(); ++i)
                result.adxr.push_back((result.adx[i] + result.adx[i - back]) * 0.5);
        }
        return result;
    }

    std::vector<double> CciCalc(const std::vector<Bar>& bars, int period) {
        std::vector<double> cci;
        if (period <= 0) return cci;
        if (bars.size() < static_cast<std::size_t>(period)) return cci;

        std::vector<double> tp;
        tp.reserve(bars.size());
        for (const Bar& b : bars)
            tp.push_back((b.high + b.low + b.close) / 3.0);

        const std::vector<double> ma = SimpleMA(tp, period);
        cci.reserve(ma.size());
        for (int i = period - 1; i < static_cast<int>(tp.size()); ++i) {
            const double avg = ma[static_cast<std::size_t>(i - period + 1)];
            double totalDev = 0.0;
            for (int j = i - period + 1; j <= i; ++j)
                totalDev += std::abs(tp[static_cast<std::size_t>(j)] - avg);
            const double meanDev = totalDev / period;
            double value = 0.0;
            if (meanDev > 1e-9) value = (tp[static_cast<std::size_t>(i)] - avg) / (0.015 * meanDev);
            cci.push_back(value);
        }
        return cci;
    }

    std::vector<double> PsyCalc(const std::vector<Bar>& bars, int period) {
        std::vector<double> psy;
        if (period <= 0) return psy;
        if (bars.size() < static_cast<std::size_t>(period + 1)) return psy;

        std::vector<int> up;
        up.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i)
            up.push_back(bars[i].close > bars[i - 1].close ? 1 : 0);

        psy.reserve(up.size() - static_cast<std::size_t>(period) + 1);
        int sumUp = 0;
        for (int i = 0; i < static_cast<int>(up.size()); ++i) {
            sumUp += up[static_cast<std::size_t>(i)];
            if (i >= period) sumUp -= up[static_cast<std::size_t>(i - period)];
            if (i >= period - 1) psy.push_back(sumUp * 100.0 / period);
        }
        return psy;
    }

    std::vector<double> MtmCalc(const std::vector<double>& closes, int period) {
        std::vector<double> mtm;
        if (period <= 0) return mtm;
        if (closes.size() <= static_cast<std::size_t>(period)) return mtm;

        mtm.reserve(closes.size() - static_cast<std::size_t>(period));
        for (int i = period; i < static_cast<int>(closes.size()); ++i)
            mtm.push_back(closes[static_cast<std::size_t>(i)] - closes[static_cast<std::size_t>(i - period)]);
        return mtm;
    }

    std::vector<double> AdtmCalc(const std::vector<Bar>& bars, int period) {
        std::vector<double> adtm;
        if (period <= 0) return adtm;
        if (bars.size() < static_cast<std::size_t>(period + 1)) return adtm;

        std::vector<double> dtm, dbm;
        dtm.reserve(bars.size() - 1);
        dbm.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double open = bars[i].open;
            const double prevOpen = bars[i - 1].open;
            const double upAmp = std::max(bars[i].high - open, open - prevOpen);
            const double downAmp = std::max(open - bars[i].low, prevOpen - open);
            dtm.push_back(open <= prevOpen ? 0.0 : upAmp);
            dbm.push_back(open >= prevOpen ? 0.0 : downAmp);
        }

        const std::vector<double> stm = RollingSum(dtm, period);
        const std::vector<double> sbm = RollingSum(dbm, period);
        if (stm.empty()) return adtm;

        adtm.reserve(stm.size());
        for (std::size_t i = 0; i < stm.size(); ++i) {
            if (stm[i] > sbm[i])      adtm.push_back((stm[i] - sbm[i]) / stm[i]);
            else if (sbm[i] > stm[i]) adtm.push_back((stm[i] - sbm[i]) / sbm[i]);
            else                      adtm.push_back(0.0);
        }
        return adtm;
    }

    // ===================================================================
    // ===================================================================

    SeriesPlot ComputeMACD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 3u) return plot;

        const int fast = static_cast<int>(params[0]);
        const int slow = static_cast<int>(params[1]);
        const int signal = static_cast<int>(params[2]);
        if (fast <= 0 || slow <= 0 || signal <= 0) return plot;

        const MACDData macd = MacdCalc(Closes(bars), fast, slow, signal);
        if (macd.dif.size() < static_cast<std::size_t>(slow)) return plot;

        const int total = static_cast<int>(macd.dif.size());
        const int start = slow - 1;
        const int count = total - start;

        PushSignBars(plot.series, macd.histogram, static_cast<std::size_t>(start),
            static_cast<std::size_t>(count), start,
            Rgb(1.0f, 0.0f, 0.0f), Rgb(0.0f, 0.8f, 0.0f));

        plot.series.push_back(SegmentSeries(static_cast<float>(start), 0.0f,
            static_cast<float>(total - 1), 0.0f, Rgb(0.5f, 0.5f, 0.5f)));

        plot.series.push_back(LineSeries(macd.dif, static_cast<std::size_t>(start),
            static_cast<std::size_t>(count), start, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(macd.dea, static_cast<std::size_t>(start),
            static_cast<std::size_t>(count), start, Rgb(1.0f, 0.9f, 0.0f)));

        plot.xMin = static_cast<float>(start);
        plot.xMax = static_cast<float>(total - 1);

        double yMin = 0.0, yMax = 0.0;
        for (int i = start; i < total; ++i) {
            const std::size_t k = static_cast<std::size_t>(i);
            yMin = std::min({ yMin, macd.dif[k], macd.dea[k], macd.histogram[k] });
            yMax = std::max({ yMax, macd.dif[k], macd.dea[k], macd.histogram[k] });
        }
        const double yRange = yMax - yMin;
        plot.yMin = static_cast<float>(yMin - yRange * 0.1);
        plot.yMax = static_cast<float>(yMax + yRange * 0.1);

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeKDJ(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 3u) return plot;

        const int N = static_cast<int>(params[0]);
        const int M1 = static_cast<int>(params[1]);
        const int M2 = static_cast<int>(params[2]);
        if (N <= 0 || M1 <= 0 || M2 <= 0) return plot;

        const KDJData kdj = KdjCalc(bars, N, M1, M2);
        if (kdj.K.empty()) return plot;

        const int total = static_cast<int>(kdj.K.size());
        const int startIndex = N - 1;

        plot.series.push_back(LineSeries(kdj.K, startIndex, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(kdj.D, startIndex, Rgb(1.0f, 0.85f, 0.0f)));
        plot.series.push_back(LineSeries(kdj.J, startIndex, Rgb(0.8f, 0.4f, 1.0f)));

        plot.xMin = static_cast<float>(startIndex);
        plot.xMax = static_cast<float>(startIndex + total - 1);

        float yMin = 0.0f, yMax = 100.0f;
        ExtendRangeF(kdj.K, yMin, yMax);
        ExtendRangeF(kdj.D, yMin, yMax);
        ExtendRangeF(kdj.J, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.5f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeKD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 3u) return plot;

        const int N = static_cast<int>(params[0]);
        const int M1 = static_cast<int>(params[1]);
        const int M2 = static_cast<int>(params[2]);
        if (N <= 0 || M1 <= 0 || M2 <= 0) return plot;

        const KDJData kdj = KdjCalc(bars, N, M1, M2);
        if (kdj.K.empty()) return plot;

        const int total = static_cast<int>(kdj.K.size());
        const int startIndex = N - 1;

        plot.series.push_back(LineSeries(kdj.K, startIndex, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(kdj.D, startIndex, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(startIndex);
        plot.xMax = static_cast<float>(startIndex + total - 1);

        float yMin = 0.0f, yMax = 100.0f;
        ExtendRangeF(kdj.K, yMin, yMax);
        ExtendRangeF(kdj.D, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.5f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeROC(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int rocPeriod = static_cast<int>(params[0]);
        const int maPeriod = static_cast<int>(params[1]);
        if (rocPeriod <= 0 || maPeriod <= 0) return plot;

        const std::vector<double> rocValues = RocCalc(bars, rocPeriod);
        if (rocValues.empty()) return plot;

        const int total = static_cast<int>(rocValues.size());
        const int startIdx = rocPeriod;

        std::vector<double> maValues;
        maValues.reserve(static_cast<std::size_t>(total));
        double sum = 0.0;
        for (int i = 0; i < total; ++i) {
            sum += rocValues[static_cast<std::size_t>(i)];
            if (i >= maPeriod) {
                sum -= rocValues[static_cast<std::size_t>(i - maPeriod)];
                maValues.push_back(sum / maPeriod);
            }
            else if (i == maPeriod - 1) {
                maValues.push_back(sum / maPeriod);
            }
            else {
                maValues.push_back(0.0);
            }
        }

        const int maStartIdx = startIdx + maPeriod - 1;
        const int maCount = total - maPeriod + 1;
        const std::size_t maBegin = static_cast<std::size_t>(maPeriod - 1);
        const std::size_t maTake = (maCount > 0) ? static_cast<std::size_t>(maCount) : 0u;

        plot.series.push_back(LineSeries(rocValues, startIdx, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(maValues, maBegin, maTake, maStartIdx,
            Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = static_cast<float>(startIdx);
        plot.xMax = static_cast<float>(startIdx + total - 1);

        float yMin = std::numeric_limits<float>::max();
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(rocValues, yMin, yMax);
        ExtendRangeF(maValues, yMin, yMax);
        if (yMin > 0.0f) yMin = 0.0f;
        if (yMax < 0.0f) yMax = 0.0f;
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeRSI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int period1 = static_cast<int>(params[0]);
        const int period2 = static_cast<int>(params[1]);
        if (period1 <= 0 || period2 <= 0) return plot;

        const std::vector<double> closes = Closes(bars);
        const std::vector<double> rsi1 = RsiCalc(closes, period1);
        const std::vector<double> rsi2 = RsiCalc(closes, period2);
        if (rsi1.empty() || rsi2.empty()) return plot;

        const int total1 = static_cast<int>(rsi1.size());
        const int total2 = static_cast<int>(rsi2.size());

        plot.series.push_back(LineSeries(rsi1, period1, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(rsi2, period2, Rgb(1.0f, 0.85f, 0.0f)));

        const int minStart = std::min(period1, period2);
        const int maxEnd = std::max(period1 + total1 - 1, period2 + total2 - 1);
        plot.xMin = static_cast<float>(minStart);
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = 0.0f, yMax = 100.0f;
        ExtendRangeF(rsi1, yMin, yMax);
        ExtendRangeF(rsi2, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeSLOWKD(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 4u) return plot;

        const int N = static_cast<int>(params[0]);
        const int M1 = static_cast<int>(params[1]);
        const int M2 = static_cast<int>(params[2]);
        const int M3 = static_cast<int>(params[3]);
        if (N <= 0 || M1 <= 0 || M2 <= 0 || M3 <= 0) return plot;

        const SlowKDData slowKD = SlowKdCalc(bars, N, M1, M2, M3);
        if (slowKD.slowK.empty() || slowKD.slowD.empty()) return plot;

        const int totalK = static_cast<int>(slowKD.slowK.size());
        const int totalD = static_cast<int>(slowKD.slowD.size());
        const int startK = N - 1;
        const int startD = startK;

        plot.series.push_back(LineSeries(slowKD.slowK, startK, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(slowKD.slowD, startD, Rgb(1.0f, 0.85f, 0.0f)));

        const int maxX = std::max(startK + totalK - 1, startD + totalD - 1);
        plot.xMin = static_cast<float>(startK);
        plot.xMax = static_cast<float>(maxX);

        float yMin = 0.0f, yMax = 100.0f;
        ExtendRangeF(slowKD.slowK, yMin, yMax);
        ExtendRangeF(slowKD.slowD, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeWR(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int period = static_cast<int>(params[0]);
        if (period <= 0) return plot;

        const std::vector<double> wrValues = WrCalc(bars, period);
        if (wrValues.empty()) return plot;

        const int total = static_cast<int>(wrValues.size());
        const int startIdx = period - 1;

        plot.series.push_back(LineSeries(wrValues, startIdx, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(startIdx);
        plot.xMax = static_cast<float>(startIdx + total - 1);

        float yMin = -100.0f, yMax = 0.0f;
        ExtendRangeF(wrValues, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeBIAS(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 3u) return plot;

        const int period1 = static_cast<int>(params[0]);
        const int period2 = static_cast<int>(params[1]);
        const int period3 = static_cast<int>(params[2]);
        if (period1 <= 0 || period2 <= 0 || period3 <= 0) return plot;

        const std::vector<double> closes = Closes(bars);
        const std::vector<double> bias1 = BiasCalc(closes, period1);
        const std::vector<double> bias2 = BiasCalc(closes, period2);
        const std::vector<double> bias3 = BiasCalc(closes, period3);
        if (bias1.empty() || bias2.empty() || bias3.empty()) return plot;

        const int total1 = static_cast<int>(bias1.size());
        const int total2 = static_cast<int>(bias2.size());
        const int total3 = static_cast<int>(bias3.size());
        const int start1 = period1 - 1;
        const int start2 = period2 - 1;
        const int start3 = period3 - 1;

        plot.series.push_back(LineSeries(bias1, start1, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(bias2, start2, Rgb(1.0f, 0.85f, 0.0f)));
        plot.series.push_back(LineSeries(bias3, start3, Rgb(0.8f, 0.4f, 1.0f)));

        const int minStart = std::min({ start1, start2, start3 });
        const int maxEnd = std::max({ start1 + total1 - 1, start2 + total2 - 1,
                                      start3 + total3 - 1 });
        plot.xMin = static_cast<float>(minStart);
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(bias1, yMin, yMax);
        ExtendRangeF(bias2, yMin, yMax);
        ExtendRangeF(bias3, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeCR(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 5u) return plot;

        const int N = static_cast<int>(params[0]);
        const int maPeriod[4] = { static_cast<int>(params[1]), static_cast<int>(params[2]),
                                  static_cast<int>(params[3]), static_cast<int>(params[4]) };
        if (N <= 0) return plot;
        for (int i = 0; i < 4; ++i)
            if (maPeriod[i] <= 0) return plot;

        const std::vector<double> crValues = CrCalc(bars, N);
        if (crValues.empty()) return plot;

        const int startCR = N;
        const int totalCR = static_cast<int>(crValues.size());

        plot.series.push_back(LineSeries(crValues, startCR, Rgb(1.0f, 1.0f, 1.0f)));

        const Color4f palette[4] = { Rgb(1.0f, 0.85f, 0.0f), Rgb(0.8f, 0.4f, 1.0f),
                                     Rgb(0.2f, 0.8f, 0.2f), Rgb(0.5f, 0.5f, 0.5f) };

        int maxX = startCR + totalCR - 1;

        float yMin = std::numeric_limits<float>::max();
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(crValues, yMin, yMax);

        for (int k = 0; k < 4; ++k) {
            const std::vector<double> ma = SimpleMA(crValues, maPeriod[k]);
            const int shift = static_cast<int>(maPeriod[k] / 2.5) + 1;
            const int left = static_cast<int>(ma.size()) - shift;

            if (left <= 0) {
                plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0, palette[k]));
                continue;
            }

            const int start = startCR + maPeriod[k] - 1 + shift;
            plot.series.push_back(LineSeries(ma, 0u, static_cast<std::size_t>(left),
                start, palette[k]));

            for (int j = 0; j < left; ++j) {
                yMin = std::min(yMin, static_cast<float>(ma[static_cast<std::size_t>(j)]));
                yMax = std::max(yMax, static_cast<float>(ma[static_cast<std::size_t>(j)]));
            }
            maxX = std::max(maxX, start + left - 1);
        }

        plot.xMin = static_cast<float>(startCR);
        plot.xMax = static_cast<float>(maxX);

        float margin = (yMax - yMin) * 0.1f;
        if (margin < 5.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeATR(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int period = static_cast<int>(params[0]);
        if (period <= 0) return plot;

        const ATRData atrData = AtrCalc(bars, period);
        if (atrData.tr.empty() || atrData.atr.empty()) return plot;

        const int totalTR = static_cast<int>(atrData.tr.size());
        const int totalATR = static_cast<int>(atrData.atr.size());
        const int startATR = period;

        plot.series.push_back(LineSeries(atrData.tr, 1, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(atrData.atr, startATR, Rgb(1.0f, 0.85f, 0.0f)));

        const int maxEnd = std::max(1 + totalTR - 1, startATR + totalATR - 1);
        plot.xMin = 1.0f;
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = std::numeric_limits<float>::max();
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(atrData.tr, yMin, yMax);
        ExtendRangeF(atrData.atr, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.01f) margin = 0.1f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeDMI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int diPeriod = static_cast<int>(params[0]);
        const int adxPeriod = static_cast<int>(params[1]);
        if (diPeriod <= 0 || adxPeriod <= 0) return plot;

        const DMIData dmi = DmiCalc(bars, diPeriod, adxPeriod);
        if (dmi.plusDI.empty() || dmi.minusDI.empty() || dmi.adx.empty() || dmi.adxr.empty())
            return plot;

        const int startDI = diPeriod;
        const int startADX = startDI + adxPeriod - 1;
        const int startADXR = startADX + adxPeriod;

        plot.series.push_back(LineSeries(dmi.plusDI, startDI, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(dmi.minusDI, startDI, Rgb(1.0f, 0.85f, 0.0f)));
        plot.series.push_back(LineSeries(dmi.adx, startADX, Rgb(0.8f, 0.4f, 1.0f)));
        plot.series.push_back(LineSeries(dmi.adxr, startADXR, Rgb(0.2f, 0.8f, 0.2f)));

        const int maxX = std::max(startDI + static_cast<int>(dmi.plusDI.size()) - 1,
            startADXR + static_cast<int>(dmi.adxr.size()) - 1);
        plot.xMin = static_cast<float>(startDI);
        plot.xMax = static_cast<float>(maxX);

        float yMin = 0.0f;
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(dmi.plusDI, yMin, yMax);
        ExtendRangeF(dmi.minusDI, yMin, yMax);
        ExtendRangeF(dmi.adx, yMin, yMax);
        ExtendRangeF(dmi.adxr, yMin, yMax);

        const float upper = std::max(100.0f, yMax);
        float margin = upper * 0.1f;
        if (margin < 5.0f) margin = 5.0f;
        plot.yMin = -5.0f;
        plot.yMax = upper + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeCCI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int period = static_cast<int>(params[0]);
        if (period <= 0) return plot;

        const std::vector<double> cciValues = CciCalc(bars, period);
        if (cciValues.empty()) return plot;

        const int total = static_cast<int>(cciValues.size());
        const int startIdx = period - 1;

        plot.series.push_back(LineSeries(cciValues, startIdx, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(startIdx);
        plot.xMax = static_cast<float>(startIdx + total - 1);

        float yMin = std::numeric_limits<float>::max();
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(cciValues, yMin, yMax);
        yMin = std::min(yMin, -100.0f);
        yMax = std::max(yMax, 100.0f);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 10.0f) margin = 10.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputePSY(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int period = static_cast<int>(params[0]);
        const int maPeriod = static_cast<int>(params[1]);
        if (period <= 0 || maPeriod <= 0) return plot;

        const std::vector<double> psyValues = PsyCalc(bars, period);
        if (psyValues.empty()) return plot;

        const std::vector<double> maValues = SimpleMA(psyValues, maPeriod);

        const int startPSY = period;
        const int startMA = startPSY + maPeriod - 1;

        plot.series.push_back(LineSeries(psyValues, startPSY, Rgb(1.0f, 1.0f, 1.0f)));
        if (!maValues.empty())
            plot.series.push_back(LineSeries(maValues, startMA, Rgb(1.0f, 0.85f, 0.0f)));

        int maxEnd = startPSY + static_cast<int>(psyValues.size()) - 1;
        if (!maValues.empty())
            maxEnd = std::max(maxEnd, startMA + static_cast<int>(maValues.size()) - 1);
        plot.xMin = static_cast<float>(startPSY);
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = 0.0f, yMax = 100.0f;
        ExtendRangeF(psyValues, yMin, yMax);
        ExtendRangeF(maValues, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 5.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeMTM(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int mtmPeriod = static_cast<int>(params[0]);
        const int maPeriod = static_cast<int>(params[1]);
        if (mtmPeriod <= 0 || maPeriod <= 0) return plot;

        const std::vector<double> mtmValues = MtmCalc(Closes(bars), mtmPeriod);
        if (mtmValues.empty()) return plot;

        const std::vector<double> maValues = SimpleMA(mtmValues, maPeriod);

        const int startMTM = mtmPeriod;
        const int startMA = startMTM + maPeriod - 1;

        plot.series.push_back(LineSeries(mtmValues, startMTM, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(maValues, startMA, Rgb(1.0f, 0.85f, 0.0f)));

        const int maxEnd = std::max(startMTM + static_cast<int>(mtmValues.size()) - 1,
            startMA + static_cast<int>(maValues.size()) - 1);
        plot.xMin = static_cast<float>(startMTM);
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = std::numeric_limits<float>::max();
        float yMax = std::numeric_limits<float>::lowest();
        ExtendRangeF(mtmValues, yMin, yMax);
        ExtendRangeF(maValues, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 1.0f) margin = 5.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeDDI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 4u) return plot;

        const int N = static_cast<int>(params[0]);
        const int n1 = static_cast<int>(params[1]);
        const int m = static_cast<int>(params[2]);
        const int m1 = static_cast<int>(params[3]);
        if (N <= 0 || n1 <= 0 || m <= 0 || m1 <= 0 || m > n1) return plot;
        if (bars.size() < static_cast<std::size_t>(N + 1)) return plot;

        std::vector<double> dmz, dmf;
        dmz.reserve(bars.size() - 1);
        dmf.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double curSum = bars[i].high + bars[i].low;
            const double prevHigh = bars[i - 1].high;
            const double prevLow = bars[i - 1].low;
            const double prevSum = prevHigh + prevLow;
            const double amp = std::max(std::abs(bars[i].high - prevHigh),
                                        std::abs(bars[i].low - prevLow));
            dmz.push_back(curSum <= prevSum ? 0.0 : amp);
            dmf.push_back(curSum >= prevSum ? 0.0 : amp);
        }

        const std::vector<double> sumZ = RollingSum(dmz, N);
        const std::vector<double> sumF = RollingSum(dmf, N);
        if (sumZ.empty()) return plot;

        std::vector<double> ddi;
        ddi.reserve(sumZ.size());
        for (std::size_t i = 0; i < sumZ.size(); ++i) {
            const double total = sumZ[i] + sumF[i];
            ddi.push_back(total == 0.0 ? 0.0 : (sumZ[i] - sumF[i]) / total);
        }

        const std::vector<double> addi = RecursiveSmaW(ddi, n1, m);
        const std::vector<double> ad = SimpleMA(addi, m1);

        const int startDDI = N;
        const int startADDI = startDDI;
        const int startAD = startDDI + m1 - 1;

        int xMax = startDDI + static_cast<int>(ddi.size()) - 1;

        PushSignBars(plot.series, ddi, 0, ddi.size(), startDDI,
            Rgb(1.0f, 0.0f, 0.0f), Rgb(0.0f, 0.8f, 0.8f));

        plot.series.push_back(SegmentSeries(static_cast<float>(startDDI), 0.0f,
            static_cast<float>(xMax), 0.0f, Rgb(0.5f, 0.5f, 0.5f)));

        if (!addi.empty()) {
            plot.series.push_back(LineSeries(addi, startADDI, Rgb(1.0f, 0.85f, 0.0f)));
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(1.0f, 0.85f, 0.0f)));
        }

        if (!ad.empty()) {
            plot.series.push_back(LineSeries(ad, startAD, Rgb(0.8f, 0.4f, 1.0f)));
            xMax = std::max(xMax, startAD + static_cast<int>(ad.size()) - 1);
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(0.8f, 0.4f, 1.0f)));
        }

        plot.xMin = static_cast<float>(startDDI);
        plot.xMax = static_cast<float>(xMax);

        float yMin = -1.0f, yMax = 1.0f;
        ExtendRangeF(ddi, yMin, yMax);
        ExtendRangeF(addi, yMin, yMax);
        ExtendRangeF(ad, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.1f) margin = 0.1f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeDMA(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 3u) return plot;

        const int n1 = static_cast<int>(params[0]);
        const int n2 = static_cast<int>(params[1]);
        const int m = static_cast<int>(params[2]);
        if (n1 <= 0 || n2 <= 0 || m <= 0) return plot;

        const std::vector<double> closes = Closes(bars);
        const int total = static_cast<int>(closes.size());

        const std::vector<double> ma1 = SimpleMA(closes, n1);
        const std::vector<double> ma2 = SimpleMA(closes, n2);
        if (ma1.empty() || ma2.empty()) return plot;

        const int startDDD = std::max(n1, n2) - 1;
        std::vector<double> ddd;
        ddd.reserve(static_cast<std::size_t>(total - startDDD));
        for (int i = startDDD; i < total; ++i) {
            const double a = ma1[static_cast<std::size_t>(i - (n1 - 1))];
            const double b = ma2[static_cast<std::size_t>(i - (n2 - 1))];
            ddd.push_back(a - b);
        }
        if (ddd.empty()) return plot;

        const std::vector<double> ama = SimpleMA(ddd, m);
        const int startAMA = startDDD + m - 1;

        int xMax = startDDD + static_cast<int>(ddd.size()) - 1;

        plot.series.push_back(LineSeries(ddd, startDDD, Rgb(1.0f, 1.0f, 1.0f)));
        if (!ama.empty()) {
            plot.series.push_back(LineSeries(ama, startAMA, Rgb(1.0f, 0.85f, 0.0f)));
            xMax = std::max(xMax, startAMA + static_cast<int>(ama.size()) - 1);
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(1.0f, 0.85f, 0.0f)));
        }

        plot.xMin = static_cast<float>(startDDD);
        plot.xMax = static_cast<float>(xMax);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(ddd, yMin, yMax);
        ExtendRangeF(ama, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.5f) margin = 0.5f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeADTM(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 2u) return plot;

        const int period = static_cast<int>(params[0]);
        const int maPeriod = static_cast<int>(params[1]);
        if (period <= 0 || maPeriod <= 0) return plot;

        const std::vector<double> adtmValues = AdtmCalc(bars, period);
        if (adtmValues.empty()) return plot;

        const std::vector<double> maValues = SimpleMA(adtmValues, maPeriod);

        const int startADTM = period;
        const int startMA = startADTM + maPeriod - 1;

        plot.series.push_back(LineSeries(adtmValues, startADTM, Rgb(1.0f, 1.0f, 1.0f)));
        if (!maValues.empty()) {
            plot.series.push_back(LineSeries(maValues, startMA, Rgb(1.0f, 1.0f, 0.0f)));
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(1.0f, 1.0f, 0.0f)));
        }

        int maxEnd = startADTM + static_cast<int>(adtmValues.size()) - 1;
        if (!maValues.empty())
            maxEnd = std::max(maxEnd, startMA + static_cast<int>(maValues.size()) - 1);
        plot.xMin = static_cast<float>(startADTM);
        plot.xMax = static_cast<float>(maxEnd);

        float yMin = -1.0f, yMax = 1.0f;
        ExtendRangeF(adtmValues, yMin, yMax);
        ExtendRangeF(maValues, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.1f) margin = 0.1f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeARBR(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;
        if (bars.size() < static_cast<std::size_t>(N + 1)) return plot;

        std::vector<double> arValues, brValues;
        arValues.reserve(bars.size() - static_cast<std::size_t>(N));
        brValues.reserve(bars.size() - static_cast<std::size_t>(N));
        for (std::size_t i = static_cast<std::size_t>(N); i < bars.size(); ++i) {
            double sumARUp = 0.0, sumARDown = 0.0;
            double sumBRUp = 0.0, sumBRDown = 0.0;
            for (int j = static_cast<int>(i) - N + 1; j <= static_cast<int>(i); ++j) {
                const Bar& cj = bars[static_cast<std::size_t>(j)];
                sumARUp += (cj.high - cj.open);
                sumARDown += (cj.open - cj.low);

                const double prevClose = bars[static_cast<std::size_t>(j - 1)].close;
                sumBRUp += std::max(0.0, cj.high - prevClose);
                sumBRDown += std::max(0.0, prevClose - cj.low);
            }
            arValues.push_back((sumARDown > 1e-9) ? (sumARUp / sumARDown * 100.0) : 100.0);
            brValues.push_back((sumBRDown > 1e-9) ? (sumBRUp / sumBRDown * 100.0) : 100.0);
        }

        const int startIndex = N;
        const float xLeft = static_cast<float>(startIndex);
        const float xRight = static_cast<float>(startIndex + static_cast<int>(arValues.size()) - 1);

        const Color4f gray = Rgb(0.4f, 0.4f, 0.4f);
        plot.series.push_back(SegmentSeries(xLeft, 300.0f, xRight, 300.0f, gray));
        plot.series.push_back(SegmentSeries(xLeft, 200.0f, xRight, 200.0f, gray));
        plot.series.push_back(SegmentSeries(xLeft, 100.0f, xRight, 100.0f, gray));
        plot.series.push_back(LineSeries(arValues, startIndex, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(brValues, startIndex, Rgb(1.0f, 0.85f, 0.0f)));

        plot.xMin = xLeft;
        plot.xMax = xRight;

        float yMin = 0.0f, yMax = 300.0f;
        ExtendRangeF(arValues, yMin, yMax);
        ExtendRangeF(brValues, yMin, yMax);
        plot.yMin = yMin;
        plot.yMax = yMax;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeLON(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;
        if (bars.size() < 4) return plot;

        std::vector<double> rc;
        rc.reserve(bars.size() - 1);
        for (std::size_t i = 2; i < bars.size(); ++i) {
            const double vol2 = bars[i].volume + bars[i - 1].volume;
            const double hi2 = std::max(bars[i].high, bars[i - 1].high);
            const double lo2 = std::min(bars[i].low, bars[i - 1].low);
            const double span = (hi2 - lo2) * 100.0;
            const double vid = (span == 0.0) ? 0.0 : vol2 / span;
            rc.push_back((bars[i].close - bars[i - 1].close) * vid);
        }
        if (rc.empty()) return plot;

        std::vector<double> longSum(rc.size(), 0.0);
        double acc = 0.0;
        for (std::size_t i = 0; i < rc.size(); ++i) { acc += rc[i]; longSum[i] = acc; }

        const std::vector<double> fast = RecursiveSMA(longSum, N);
        const std::vector<double> slow = RecursiveSMA(longSum, 2 * N);
        if (fast.size() != longSum.size() || slow.size() != longSum.size()) return plot;

        std::vector<double> lon(longSum.size());
        for (std::size_t i = 0; i < longSum.size(); ++i) lon[i] = fast[i] - slow[i];

        const std::vector<double> longMa = SimpleMA(lon, N);

        const int startLON = 2;
        const int total = static_cast<int>(lon.size());
        const int startMA = startLON + N - 1;

        PushSignBars(plot.series, lon, 0, lon.size(), startLON,
            Rgb(1.0f, 0.0f, 0.0f), Rgb(0.0f, 0.8f, 0.0f));

        plot.series.push_back(SegmentSeries(static_cast<float>(startLON), 0.0f,
            static_cast<float>(startLON + total - 1), 0.0f, Rgb(0.5f, 0.5f, 0.5f)));
        plot.series.push_back(LineSeries(lon, startLON, Rgb(1.0f, 1.0f, 1.0f)));

        int xMax = startLON + total - 1;
        if (!longMa.empty()) {
            plot.series.push_back(LineSeries(longMa, startMA, Rgb(1.0f, 0.85f, 0.0f)));
            xMax = std::max(xMax, startMA + static_cast<int>(longMa.size()) - 1);
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(1.0f, 0.85f, 0.0f)));
        }

        plot.xMin = static_cast<float>(startLON);
        plot.xMax = static_cast<float>(xMax);

        double yMin = 0.0, yMax = 0.0;
        ExtendRange(lon, yMin, yMax);
        ExtendRange(longMa, yMin, yMax);
        double margin = (yMax - yMin) * 0.1;
        if (margin < 1e-6) margin = 0.1;
        plot.yMin = static_cast<float>(yMin - margin);
        plot.yMax = static_cast<float>(yMax + margin);

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeSRDM(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;
        if (bars.size() < 12) return plot;

        std::vector<double> dmz, dmf;
        dmz.reserve(bars.size() - 1);
        dmf.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double curSum = bars[i].high + bars[i].low;
            const double prevSum = bars[i - 1].high + bars[i - 1].low;
            const double amp = std::max(std::abs(bars[i].high - bars[i - 1].high),
                                        std::abs(bars[i].low - bars[i - 1].low));
            dmz.push_back(curSum <= prevSum ? 0.0 : amp);
            dmf.push_back(curSum >= prevSum ? 0.0 : amp);
        }

        const int kAdmPeriod = 10;
        const std::vector<double> admz = SimpleMA(dmz, kAdmPeriod);
        const std::vector<double> admf = SimpleMA(dmf, kAdmPeriod);
        if (admz.empty()) return plot;

        std::vector<double> srdm;
        srdm.reserve(admz.size());
        for (std::size_t i = 0; i < admz.size(); ++i) {
            const double z = admz[i], f = admf[i];
            if (z > f)      srdm.push_back((z - f) / z);
            else if (f > z) srdm.push_back((z - f) / f);
            else            srdm.push_back(0.0);
        }

        const std::vector<double> asrdm = RecursiveSMA(srdm, N);
        const int startSRDM = kAdmPeriod;

        plot.series.push_back(LineSeries(srdm, startSRDM, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(asrdm, startSRDM, Rgb(1.0f, 1.0f, 0.0f)));

        plot.xMin = static_cast<float>(startSRDM);
        plot.xMax = static_cast<float>(startSRDM + static_cast<int>(srdm.size()) - 1);

        float yMin = -1.0f, yMax = 1.0f;
        ExtendRangeF(srdm, yMin, yMax);
        ExtendRangeF(asrdm, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.1f) margin = 0.1f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeSHORT(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;
        if (bars.size() < 25) return plot;

        const int kVolPeriod = 5;
        const int kMaPeriod = 24;

        std::vector<double> volRate;
        volRate.reserve(bars.size() - 1);
        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double prevVol = bars[i - 1].volume;
            volRate.push_back(prevVol > 0.0 ? (bars[i].volume - prevVol) / prevVol : 0.0);
        }
        const std::vector<double> vol1 = SimpleMA(volRate, kVolPeriod);
        if (vol1.empty()) return plot;

        const std::vector<double> closes = Closes(bars);
        const std::vector<double> maC = SimpleMA(closes, kMaPeriod);
        if (maC.empty()) return plot;

        const int startSHORT = kMaPeriod - 1;
        std::vector<double> shortVals;
        shortVals.reserve(bars.size() - static_cast<std::size_t>(startSHORT));
        for (int t = startSHORT; t < static_cast<int>(bars.size()); ++t) {
            const int jv = t - kVolPeriod;
            const int jm = t - (kMaPeriod - 1);
            if (jv < 0 || jv >= static_cast<int>(vol1.size())) continue;
            if (jm < 0 || jm >= static_cast<int>(maC.size())) continue;
            const std::size_t sj = static_cast<std::size_t>(jm);
            const double jc2 = (closes[static_cast<std::size_t>(t)] - maC[sj])
                / maC[sj] * 100.0;
            shortVals.push_back(jc2 * (1.0 + vol1[static_cast<std::size_t>(jv)]));
        }
        if (shortVals.empty()) return plot;

        const std::vector<double> maVals = SimpleMA(shortVals, N);

        const int total = static_cast<int>(shortVals.size());
        const int startMA = startSHORT + N - 1;

        PushSignBars(plot.series, shortVals, 0, shortVals.size(), startSHORT,
            Rgb(1.0f, 0.0f, 0.0f), Rgb(0.0f, 0.8f, 0.0f));

        plot.series.push_back(SegmentSeries(static_cast<float>(startSHORT), 0.0f,
            static_cast<float>(startSHORT + total - 1), 0.0f, Rgb(0.5f, 0.5f, 0.5f)));
        plot.series.push_back(LineSeries(shortVals, startSHORT, Rgb(1.0f, 1.0f, 1.0f)));

        int xMax = startSHORT + total - 1;
        if (!maVals.empty()) {
            plot.series.push_back(LineSeries(maVals, startMA, Rgb(1.0f, 0.85f, 0.0f)));
            xMax = std::max(xMax, startMA + static_cast<int>(maVals.size()) - 1);
        }
        else {
            plot.series.push_back(LineSeries(std::vector<double>(), 0u, 0u, 0,
                Rgb(1.0f, 0.85f, 0.0f)));
        }

        plot.xMin = static_cast<float>(startSHORT);
        plot.xMax = static_cast<float>(xMax);

        double yMin = 0.0, yMax = 0.0;
        ExtendRange(shortVals, yMin, yMax);
        ExtendRange(maVals, yMin, yMax);
        double margin = (yMax - yMin) * 0.1;
        if (margin < 1e-6) margin = 1e-3;
        plot.yMin = static_cast<float>(yMin - margin);
        plot.yMax = static_cast<float>(yMax + margin);

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeMI(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;

        int M = (params.size() >= 2u) ? static_cast<int>(params[1]) : N;
        if (M <= 0) M = N;

        const std::vector<double> closes = Closes(bars);
        const int size = static_cast<int>(closes.size());
        if (size < N + 1) return plot;

        std::vector<double> aVals;
        aVals.reserve(static_cast<std::size_t>(size - N));
        for (int i = N; i < size; ++i)
            aVals.push_back(closes[static_cast<std::size_t>(i)] -
                closes[static_cast<std::size_t>(i - N)]);

        const std::vector<double> miVals = RecursiveSMA(aVals, M);

        const int startWhite = N;

        plot.series.push_back(LineSeries(aVals, startWhite, Rgb(1.0f, 1.0f, 1.0f)));
        plot.series.push_back(LineSeries(miVals, startWhite, Rgb(1.0f, 1.0f, 0.0f)));

        plot.xMin = static_cast<float>(startWhite);
        plot.xMax = static_cast<float>(startWhite + static_cast<int>(aVals.size()) - 1);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(aVals, yMin, yMax);
        ExtendRangeF(miVals, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.5f) margin = 0.5f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeDPO(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (params.size() < 1u) return plot;

        const int N = static_cast<int>(params[0]);
        if (N <= 0) return plot;

        const std::vector<double> closes = Closes(bars);
        const int size = static_cast<int>(closes.size());
        const int smaPeriod = N;
        const int shift = N / 2 + 1;
        if (size < smaPeriod + shift) return plot;

        const std::vector<double> sma = SimpleMA(closes, smaPeriod);
        if (sma.empty()) return plot;

        const int startDPO = smaPeriod - 1 + shift;
        std::vector<double> dpo;
        dpo.reserve(static_cast<std::size_t>(size - startDPO));
        for (int i = startDPO; i < size; ++i)
            dpo.push_back(closes[static_cast<std::size_t>(i)] -
                sma[static_cast<std::size_t>(i - shift - (smaPeriod - 1))]);
        if (dpo.empty()) return plot;

        plot.series.push_back(LineSeries(dpo, startDPO, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = static_cast<float>(startDPO);
        plot.xMax = static_cast<float>(startDPO + static_cast<int>(dpo.size()) - 1);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(dpo, yMin, yMax);
        float margin = (yMax - yMin) * 0.1f;
        if (margin < 0.01f) margin = 0.1f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

    SeriesPlot ComputeASl(const std::vector<Bar>& bars, const std::vector<double>& params) {
        SeriesPlot plot;
        if (!params.empty()) return plot;
        if (bars.size() < 2) return plot;

        std::vector<double> asi;
        asi.reserve(bars.size());
        asi.push_back(0.0);
        double prevASI = 0.0;

        for (std::size_t i = 1; i < bars.size(); ++i) {
            const double prevClose = bars[i - 1].close;
            const double prevOpen = bars[i - 1].open;
            const double prevLow = bars[i - 1].low;
            const double open = bars[i].open;
            const double high = bars[i].high;
            const double low = bars[i].low;
            const double close = bars[i].close;

            const double aa = std::abs(high - prevClose);
            const double bb = std::abs(low - prevClose);
            const double cc = std::abs(high - prevLow);
            const double dd = std::abs(prevClose - prevOpen);

            double r = 0.0;
            if (aa > bb && aa > cc)      r = aa + 0.5 * bb + 0.25 * dd;
            else if (bb > aa && bb > cc) r = bb + 0.5 * aa + 0.25 * dd;
            else                         r = cc + 0.25 * dd;

            const double k = std::max(aa, bb);
            if (r > 1e-12) {
                const double x = close - prevClose + 0.5 * (close - open)
                    + (prevClose - prevOpen);
                prevASI += 16.0 * x / r * k;
            }
            asi.push_back(prevASI);
        }

        plot.series.push_back(LineSeries(asi, 0, Rgb(1.0f, 1.0f, 1.0f)));

        plot.xMin = 0.0f;
        plot.xMax = static_cast<float>(bars.size() - 1);

        float yMin = 0.0f, yMax = 0.0f;
        ExtendRangeF(asi, yMin, yMax);
        float margin = (yMax - yMin) * 0.05f;
        if (margin < 1.0f) margin = 1.0f;
        plot.yMin = yMin - margin;
        plot.yMax = yMax + margin;

        plot.valid = true;
        return plot;
    }

} // namespace

// =======================================================================
// =======================================================================

SeriesPlot ComputeOscillator(const std::vector<Bar>& bars,
    const std::string& name,
    const std::vector<double>& params) {

    if (bars.empty()) return SeriesPlot{};

    if (name == "MACD") return ComputeMACD(bars, params);
    else if (name == "KDJ") return ComputeKDJ(bars, params);
    else if (name == "KD") return ComputeKD(bars, params);
    else if (name == "ROC") return ComputeROC(bars, params);
    else if (name == "RSI") return ComputeRSI(bars, params);
    else if (name == "SLOWKD") return ComputeSLOWKD(bars, params);
    else if (name == "WR") return ComputeWR(bars, params);
    else if (name == "BIAS") return ComputeBIAS(bars, params);
    else if (name == "CR") return ComputeCR(bars, params);
    else if (name == "ATR") return ComputeATR(bars, params);
    else if (name == "DMI") return ComputeDMI(bars, params);
    else if (name == "CCI") return ComputeCCI(bars, params);
    else if (name == "PSY") return ComputePSY(bars, params);
    else if (name == "MTM") return ComputeMTM(bars, params);
    else if (name == "DDI") return ComputeDDI(bars, params);
    else if (name == "DMA") return ComputeDMA(bars, params);
    else if (name == "ADTM") return ComputeADTM(bars, params);
    else if (name == "ARBR") return ComputeARBR(bars, params);
    else if (name == "LON") return ComputeLON(bars, params);
    else if (name == "SRDM") return ComputeSRDM(bars, params);
    else if (name == "SHORT") return ComputeSHORT(bars, params);
    else if (name == "MI") return ComputeMI(bars, params);
    else if (name == "DPO") return ComputeDPO(bars, params);
    else if (name == "ASl") return ComputeASl(bars, params);

    return SeriesPlot{};
}

} // namespace zplot
