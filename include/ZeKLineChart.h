// SPDX-License-Identifier: MIT
/**
 * @file ZeKLineChart.h
 * @brief Candlestick chart: turn a sequence of Bar into body and wick triangles.
 */

#pragma once

#include "ZeBar.h"
#include "ZePlotGeometry.h"

#include <cstddef>
#include <vector>

namespace zplot {

    /**
     * @brief Drawing style for candlesticks.
     */
    struct KLineStyle {
        float bodyWidth = 9.0f;      ///< Body width (pixels)
        float wickWidth = 1.0f;      ///< Wick width (pixels)
        float minBodyHeight = 1.0f;  ///< Minimum body height (pixels)

        bool  hollowUp = true;       ///< Draw up bars as hollow (outline only)
        float borderWidth = 1.0f;    ///< Outline width when hollow (pixels)

        Color4f up = Color4f::FromHex(0xFF0000);    ///< Up (close > open): red
        Color4f down = Color4f::FromHex(0x00FFFF);  ///< Down (close < open): cyan
        Color4f flat = Color4f::FromHex(0xFFFFFF);  ///< Flat (close == open): white
    };

    /**
     * @brief Compute the price range covered by a visible slice.
     *
     * @param marginRatio Fraction of headroom added at both ends (0.05 = 5%)
     * @param outMin,outMax Output: the price range
     * @return Whether any data fell inside the range
     */
    bool PriceRange(const std::vector<Bar>& bars, float xMin, float xMax,
        float marginRatio, float& outMin, float& outMax);

    /**
     * @brief Build a viewport from the data automatically.
     * @param barCount How many of the most recent bars to show (0 = all)
     * @param marginRatio Price headroom ratio
     */
    Viewport FitViewport(const std::vector<Bar>& bars,
        float left, float top, float width, float height,
        std::size_t barCount = 0, float marginRatio = 0.05f);

    /**
     * @brief Emit candlestick vertices (appends to out, does not clear).
     *
     * Only bars inside the viewport's horizontal range are drawn.
     */
    void AppendKLine(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style);

    /**
     * @brief Emit bamboo (OHLC bar) vertices: a vertical line plus two ticks.
     */
    void AppendBamboo(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style);

    /**
     * @brief Emit close-only line vertices.
     */
    void AppendCloseLine(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style);

    /**
     * @brief Emit tower-chart vertices, drawn against the previous close.
     */
    void AppendTower(std::vector<PlotVertex>& out,
        const std::vector<Bar>& bars,
        const Viewport& vp,
        const KLineStyle& style);

} // namespace zplot
