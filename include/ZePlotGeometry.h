/**
 * @file ZePlotGeometry.h
 * @brief Plotting geometry: viewport mapping and shape triangulation.
 */

#pragma once

#include "ZePlotTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace zplot {

    using Kit::Color4f;
    using Kit::PlotVertex;

    /**
     * @brief Linear map from data coordinates to canvas pixels.
     *
     * Horizontal data value is a bar index; vertical data value is a price or
     * indicator reading. The vertical axis is flipped (larger value = smaller y).
     */
    struct Viewport {
        float left = 0.0f;      ///< Left edge of the plot area (pixels)
        float top = 0.0f;       ///< Top edge of the plot area (pixels)
        float width = 0.0f;     ///< Width of the plot area (pixels)
        float height = 0.0f;    ///< Height of the plot area (pixels)
        float xMin = 0.0f;      ///< Lowest horizontal data value
        float xMax = 1.0f;      ///< Highest horizontal data value
        float yMin = 0.0f;      ///< Lowest vertical data value
        float yMax = 1.0f;      ///< Highest vertical data value

        /// @brief Data x -> pixel x.
        float x(float v) const {
            if (xMax == xMin) return left;
            return left + (v - xMin) / (xMax - xMin) * width;
        }

        /// @brief Data y -> pixel y.
        float y(float v) const {
            if (yMax == yMin) return top + height;
            return top + (yMax - v) / (yMax - yMin) * height;
        }

        /// @brief Pixel spacing between two adjacent horizontal data values.
        float dx() const {
            if (xMax == xMin) return 0.0f;
            return width / (xMax - xMin);
        }

        /// @brief Pixel width covering the whole horizontal data span.
        float dataWidth() const { return xMax > xMin ? width : 0.0f; }
    };

    /// @brief One triangle (all three vertices share a color).
    inline void PushTriangle(std::vector<PlotVertex>& out,
        float x0, float y0, float x1, float y1, float x2, float y2,
        const Color4f& c) {
        out.push_back(PlotVertex{ x0, y0, c.r, c.g, c.b, c.a });
        out.push_back(PlotVertex{ x1, y1, c.r, c.g, c.b, c.a });
        out.push_back(PlotVertex{ x2, y2, c.r, c.g, c.b, c.a });
    }

    /// @brief Arbitrary quad -> 2 triangles.
    inline void PushQuadPoints(std::vector<PlotVertex>& out,
        float ax, float ay, float bx, float by,
        float cx, float cy, float dx, float dy,
        const Color4f& c) {
        PushTriangle(out, ax, ay, bx, by, cx, cy, c);
        PushTriangle(out, ax, ay, cx, cy, dx, dy, c);
    }

    /// @brief Axis-aligned rectangle -> 2 triangles.
    inline void PushRect(std::vector<PlotVertex>& out,
        float x0, float y0, float x1, float y1, const Color4f& c) {
        PushQuadPoints(out, x0, y0, x1, y0, x1, y1, x0, y1, c);
    }

    /**
     * @brief One thick segment -> quad -> 2 triangles.
     * @param width Line width in pixels (spread half to each side)
     */
    inline void PushSegment(std::vector<PlotVertex>& out,
        float x0, float y0, float x1, float y1, float width, const Color4f& c) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) return;

        const float hx = -dy / len * width * 0.5f;
        const float hy = dx / len * width * 0.5f;

        PushQuadPoints(out,
            x0 + hx, y0 + hy,
            x0 - hx, y0 - hy,
            x1 - hx, y1 - hy,
            x1 + hx, y1 + hy, c);
    }

    /// @brief One filled square (for scatter plots).
    inline void PushPoint(std::vector<PlotVertex>& out,
        float cx, float cy, float size, const Color4f& c) {
        const float h = size * 0.5f;
        PushRect(out, cx - h, cy - h, cx + h, cy + h, c);
    }

    /**
     * @brief Polyline -> one widened quad per segment, plus a square at every joint.
     *
     * @param xy    Point sequence, tightly packed: [x0, y0, x1, y1, ...]
     * @param count Number of points (not floats)
     * @param width Line width in pixels
     *
     * A NaN coordinate marks a break; the current run is terminated and the next
     * valid point starts a new one.
     */
    inline void PushPolyline(std::vector<PlotVertex>& out,
        const float* xy, std::size_t count, float width, const Color4f& c) {
        if (!xy || count < 2) return;

        const bool hasPrev = !(std::isnan(xy[0]) || std::isnan(xy[1]));
        float prevX = xy[0], prevY = xy[1];
        bool prevOk = hasPrev;

        for (std::size_t i = 1; i < count; ++i) {
            const float cx = xy[i * 2 + 0];
            const float cy = xy[i * 2 + 1];
            const bool ok = !(std::isnan(cx) || std::isnan(cy));

            if (ok && prevOk) {
                PushSegment(out, prevX, prevY, cx, cy, width, c);
                PushPoint(out, prevX, prevY, width, c);
            }
            prevX = cx; prevY = cy; prevOk = ok;
        }

        if (prevOk) PushPoint(out, prevX, prevY, width, c);
    }

    /**
     * @brief Polyline -> vertices, each point produced on the fly by a callback.
     * @param pointAt Returns the (x, y) of point i; return false to break
     * @param count   Number of points
     */
    template <class PointAt>
    inline void PushPolylineBy(std::vector<PlotVertex>& out, std::size_t count,
        PointAt pointAt, float width, const Color4f& c) {
        if (count < 2) return;

        bool prevOk = false;
        float prevX = 0.0f, prevY = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            float px = 0.0f, py = 0.0f;
            const bool ok = pointAt(i, px, py);
            if (ok && prevOk) {
                PushSegment(out, prevX, prevY, px, py, width, c);
                PushPoint(out, prevX, prevY, width, c);
            }
            prevX = px; prevY = py; prevOk = ok;
        }
        if (prevOk) PushPoint(out, prevX, prevY, width, c);
    }

    /**
     * @brief A bar anchored to a baseline (MACD histogram, volume bars).
     * @param yBase Pixel y of the baseline
     * @param yTop  Pixel y of the bar tip
     * @param minHeight Minimum height in pixels
     */
    inline void PushBar(std::vector<PlotVertex>& out,
        float x0, float x1, float yBase, float yTop,
        const Color4f& c, float minHeight = 1.0f) {
        float top = yTop, bottom = yBase;
        if (top > bottom) std::swap(top, bottom);
        if (bottom - top < minHeight) {
            const float mid = (top + bottom) * 0.5f;
            top = mid - minHeight * 0.5f;
            bottom = mid + minHeight * 0.5f;
        }
        PushRect(out, x0, top, x1, bottom, c);
    }

    /**
     * @brief Primitive type of one series.
     */
    enum class SeriesPrim {
        Line,   ///< Polyline
        Dot,    ///< Scatter (one square per point)
        Bar,    ///< Column from barBase to the point value
    };

    /**
     * @brief One data series.
     *
     * `xy` holds data coordinates, not pixels; pixelization happens in
     * `AppendSeries`. A NaN position marks a break.
     */
    struct Series {
        std::vector<float> xy;        ///< Point sequence [x0, y0, x1, y1, ...]
        Color4f color;                ///< Series color
        SeriesPrim prim = SeriesPrim::Line;  ///< Primitive type
        float width = 1.5f;           ///< Line stroke width / dot edge length (pixels)
        float barWidth = 4.0f;        ///< Bar column width (pixels)
        float barBase = 0.0f;         ///< Bar baseline (data-space y)

        /// @brief Number of points (not floats).
        std::size_t count() const { return xy.size() / 2; }
    };

    /**
     * @brief The complete output of one indicator: several series plus the data range.
     */
    struct SeriesPlot {
        std::vector<Series> series;   ///< The lines / dots / bars
        float xMin = 0.0f;            ///< Horizontal data range
        float xMax = 1.0f;
        float yMin = 0.0f;            ///< Vertical data range
        float yMax = 1.0f;
        bool  valid = false;          ///< Whether the indicator produced anything

        /// @brief Widen the range by a ratio on both ends.
        void PadRange(float ratio) {
            const float span = yMax - yMin;
            if (span <= 0.0f) return;
            yMin -= span * ratio;
            yMax += span * ratio;
        }

        /// @brief Darken bright (white/grey) colors so the series stay visible on a light canvas.
        void DimForLightBackground() {
            for (Series& s : series) {
                const float mn = std::min({ s.color.r, s.color.g, s.color.b });
                if (mn <= 0.5f) continue;
                s.color = Color4f(0.20f, 0.20f, 0.20f, s.color.a);
            }
        }
    };

    /**
     * @brief Turn a SeriesPlot into vertices (data coordinates -> pixels -> triangles).
     *
     * @param out  Vertex output (appends, does not clear)
     * @param plot The series produced by an indicator
     * @param vp   Viewport configured from plot's range
     */
    inline void AppendSeries(std::vector<PlotVertex>& out,
        const SeriesPlot& plot, const Viewport& vp) {

        std::vector<float> px;

        for (const Series& s : plot.series) {
            const std::size_t n = s.count();
            if (n == 0) continue;

            switch (s.prim) {
            case SeriesPrim::Line: {
                px.clear();
                px.reserve(n * 2);
                for (std::size_t i = 0; i < n; ++i) {
                    px.push_back(vp.x(s.xy[i * 2 + 0]));
                    px.push_back(vp.y(s.xy[i * 2 + 1]));
                }
                PushPolyline(out, px.data(), n, s.width, s.color);
                break;
            }
            case SeriesPrim::Dot: {
                for (std::size_t i = 0; i < n; ++i) {
                    const float cx = vp.x(s.xy[i * 2 + 0]);
                    const float cy = vp.y(s.xy[i * 2 + 1]);
                    if (std::isnan(cx) || std::isnan(cy)) continue;
                    PushPoint(out, cx, cy, s.width, s.color);
                }
                break;
            }
            case SeriesPrim::Bar: {
                const float half = s.barWidth * 0.5f;
                const float yBase = vp.y(s.barBase);
                for (std::size_t i = 0; i < n; ++i) {
                    const float cx = vp.x(s.xy[i * 2 + 0]);
                    const float cy = vp.y(s.xy[i * 2 + 1]);
                    if (std::isnan(cx) || std::isnan(cy)) continue;
                    PushBar(out, cx - half, cx + half, yBase, cy, s.color, 1.0f);
                }
                break;
            }
            }
        }
    }

} // namespace zplot
