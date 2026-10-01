/**
 * @file ZeKLineChart.cpp
 * @brief Candlestick / bamboo / close-line / tower vertex generation.
 */

#include <ZeKLineChart.h>

#include <algorithm>
#include <cmath>

namespace zplot {

    bool PriceRange(const std::vector<Bar>& bars, float xMin, float xMax,
        float marginRatio, float& outMin, float& outMax) {

        if (bars.empty()) return false;

        double lo = 0.0, hi = 0.0;
        bool any = false;

        for (const Bar& b : bars) {
            const float idx = static_cast<float>(b.index);
            if (idx < xMin || idx > xMax) continue;

            const double barLo = std::min({ b.low, b.open, b.close });
            const double barHi = std::max({ b.high, b.open, b.close });

            if (!any) {
                lo = barLo; hi = barHi; any = true;
            }
            else {
                lo = std::min(lo, barLo);
                hi = std::max(hi, barHi);
            }
        }
        if (!any) return false;

        const double span = hi - lo;
        if (span <= 0.0) {
            const double pad = std::max(std::fabs(hi) * 0.01, 1.0);
            lo -= pad;
            hi += pad;
        }
        else {
            const double pad = span * (marginRatio > 0.0f ? marginRatio : 0.0f);
            lo -= pad;
            hi += pad;
        }

        outMin = static_cast<float>(lo);
        outMax = static_cast<float>(hi);
        return true;
    }

    Viewport FitViewport(const std::vector<Bar>& bars,
        float left, float top, float width, float height,
        std::size_t barCount, float marginRatio) {

        Viewport vp;
        vp.left = left;
        vp.top = top;
        vp.width = width;
        vp.height = height;

        if (bars.empty()) {
            vp.xMin = 0.0f; vp.xMax = 1.0f;
            vp.yMin = 0.0f; vp.yMax = 1.0f;
            return vp;
        }

        std::size_t first = 0;
        if (barCount > 0 && barCount < bars.size())
            first = bars.size() - barCount;

        vp.xMin = static_cast<float>(bars[first].index);
        vp.xMax = static_cast<float>(bars.back().index);
        if (vp.xMax <= vp.xMin) vp.xMax = vp.xMin + 1.0f;

        float lo = 0.0f, hi = 1.0f;
        if (!PriceRange(bars, vp.xMin, vp.xMax, marginRatio, lo, hi)) {
            lo = 0.0f; hi = 1.0f;
        }
        vp.yMin = lo;
        vp.yMax = hi;
        return vp;
    }

    // Hollow rectangle: four border strips. Falls back to a solid fill when too narrow.
    static void PushHollowRect(std::vector<PlotVertex>& out,
        float x0, float y0, float x1, float y1, float bw, const Color4f& c) {

        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);

        if (bw <= 0.0f || (x1 - x0) < 2.0f * bw || (y1 - y0) < 2.0f * bw) {
            PushRect(out, x0, y0, x1, y1, c);
            return;
        }

        PushRect(out, x0, y0, x1, y0 + bw, c);              // top
        PushRect(out, x0, y1 - bw, x1, y1, c);              // bottom
        PushRect(out, x0, y0 + bw, x0 + bw, y1 - bw, c);    // left
        PushRect(out, x1 - bw, y0 + bw, x1, y1 - bw, c);    // right
    }

    void AppendKLine(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style) {

        if (bars.empty()) return;

        // Clamp the body width when bars are dense.
        float bw = style.bodyWidth > 0.0f ? style.bodyWidth : 1.0f;
        const float step = vp.dx();
        if (step > 0.0f && bw > step * 0.9f) bw = step * 0.9f;
        if (bw < 1.0f) bw = 1.0f;

        float ww = style.wickWidth > 0.0f ? style.wickWidth : 1.0f;
        if (ww > bw) ww = bw;

        const float halfBody = bw * 0.5f;
        const float halfWick = ww * 0.5f;

        const float visLeft = vp.left - bw;
        const float visRight = vp.left + vp.width + bw;

        out.reserve(out.size() + bars.size() * 12);

        for (const Bar& b : bars) {
            const float cx = vp.x(static_cast<float>(b.index));
            if (cx < visLeft || cx > visRight) continue;

            const Color4f c = (b.close > b.open) ? style.up
                : (b.close < b.open) ? style.down
                : style.flat;

            const float yHigh = vp.y(static_cast<float>(b.high));
            const float yLow = vp.y(static_cast<float>(b.low));
            const float yOpen = vp.y(static_cast<float>(b.open));
            const float yClose = vp.y(static_cast<float>(b.close));

            // Wick (drawn first, the body covers the middle).
            {
                float t = std::min(yHigh, yLow);
                float bt = std::max(yHigh, yLow);
                if (bt - t < 1.0f) {
                    const float mid = (t + bt) * 0.5f;
                    t = mid - 0.5f;
                    bt = mid + 0.5f;
                }
                PushRect(out, cx - halfWick, t, cx + halfWick, bt, c);
            }

            // Body (drawn on top of the wick).
            {
                float t = std::min(yOpen, yClose);
                float bt = std::max(yOpen, yClose);
                const float minH = style.minBodyHeight > 0.0f ? style.minBodyHeight : 0.0f;
                if (bt - t < minH) {
                    const float mid = (t + bt) * 0.5f;
                    t = mid - minH * 0.5f;
                    bt = mid + minH * 0.5f;
                }
                // Hollow for up candles, solid otherwise.
                if (style.hollowUp && b.close > b.open)
                    PushHollowRect(out, cx - halfBody, t, cx + halfBody, bt,
                        style.borderWidth, c);
                else
                    PushRect(out, cx - halfBody, t, cx + halfBody, bt, c);
            }
        }
    }

    void AppendBamboo(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style) {

        if (bars.empty()) return;

        float bw = style.bodyWidth > 0.0f ? style.bodyWidth : 1.0f;
        const float step = vp.dx();
        if (step > 0.0f && bw > step * 0.9f) bw = step * 0.9f;
        if (bw < 1.0f) bw = 1.0f;

        float ww = style.wickWidth > 0.0f ? style.wickWidth : 1.0f;
        if (ww > bw) ww = bw;

        const float halfBody = bw * 0.5f;
        const float halfWick = ww * 0.5f;

        const float visLeft = vp.left - bw;
        const float visRight = vp.left + vp.width + bw;

        out.reserve(out.size() + bars.size() * 18);

        for (const Bar& b : bars) {
            const float cx = vp.x(static_cast<float>(b.index));
            if (cx < visLeft || cx > visRight) continue;

            const Color4f c = (b.close > b.open) ? style.up
                : (b.close < b.open) ? style.down
                : style.flat;

            const float yOpen = vp.y(static_cast<float>(b.open));
            const float yClose = vp.y(static_cast<float>(b.close));

            // Vertical line from low to high.
            {
                float t = std::min(vp.y(static_cast<float>(b.high)),
                                   vp.y(static_cast<float>(b.low)));
                float bt = std::max(vp.y(static_cast<float>(b.high)),
                                    vp.y(static_cast<float>(b.low)));
                if (bt - t < 1.0f) {
                    const float mid = (t + bt) * 0.5f;
                    t = mid - 0.5f;
                    bt = mid + 0.5f;
                }
                PushRect(out, cx - halfWick, t, cx + halfWick, bt, c);
            }

            // Left tick at the open, right tick at the close.
            PushRect(out, cx - halfBody, yOpen - halfWick, cx, yOpen + halfWick, c);
            PushRect(out, cx, yClose - halfWick, cx + halfBody, yClose + halfWick, c);
        }
    }

    void AppendCloseLine(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style) {

        if (bars.empty()) return;

        const Color4f c = style.up;
        const float width = style.wickWidth > 0.0f ? style.wickWidth : 1.5f;

        const float lo = vp.left - vp.dx();
        const float hi = vp.left + vp.width + vp.dx();

        bool has = false;
        float prevX = 0.0f, prevY = 0.0f;

        for (const Bar& b : bars) {
            const float cx = vp.x(static_cast<float>(b.index));
            if (cx > hi) break;

            const float cy = vp.y(static_cast<float>(b.close));
            if (has && cx >= lo) PushSegment(out, prevX, prevY, cx, cy, width, c);
            prevX = cx;
            prevY = cy;
            has = true;
        }
    }

    void AppendTower(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style) {

        if (bars.empty()) return;

        float bw = style.bodyWidth > 0.0f ? style.bodyWidth : 1.0f;
        const float step = vp.dx();
        if (step > 0.0f && bw > step * 0.9f) bw = step * 0.9f;
        if (bw < 1.0f) bw = 1.0f;
        const float halfBody = bw * 0.5f;

        const float visLeft = vp.left - bw;
        const float visRight = vp.left + vp.width + bw;

        const float minH = style.minBodyHeight > 0.0f ? style.minBodyHeight : 1.0f;

        out.reserve(out.size() + bars.size() * 12);

        for (std::size_t i = 0; i < bars.size(); ++i) {
            const Bar& b = bars[i];
            const float cx = vp.x(static_cast<float>(b.index));
            if (cx < visLeft || cx > visRight) continue;

            // Compare against the previous close (the first bar uses its own open).
            const double base = (i == 0) ? bars[0].open : bars[i - 1].close;

            const Color4f c = (b.close > base) ? style.up
                : (b.close < base) ? style.down
                : style.flat;

            float y0 = vp.y(static_cast<float>(std::max(b.close, base)));
            float y1 = vp.y(static_cast<float>(std::min(b.close, base)));
            if (y1 < y0) std::swap(y0, y1);
            if (y1 - y0 < minH) {
                const float mid = (y0 + y1) * 0.5f;
                y0 = mid - minH * 0.5f;
                y1 = mid + minH * 0.5f;
            }

            PushRect(out, cx - halfBody, y0, cx + halfBody, y1, c);
        }
    }

} // namespace zplot
