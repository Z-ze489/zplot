// SPDX-License-Identifier: MIT
/**
 * @file ZeTrendIndicator.h
 * @brief Trend indicators.
 */

#pragma once

#include "ZeBar.h"
#include "ZePlotGeometry.h"

#include <string>
#include <vector>

namespace zplot {

    /**
     * @brief Compute a trend indicator.
     *
     * @param name Indicator name: `BOLL` / `MA` / `SAR` / `SAR1` / `PUBU` / `SP` /
     *             `SMA` / `EMA` / `HCL` / `MIKE` / `BBI` / `DKX` / `EMA2` /
     *             `BBlBOLL` / `CDP` / `ENV` / `TRMA` / `TSMA` / `MA扩展` /
     *             `唐奇安` / `空`
     * @param params Indicator parameters (order depends on name)
     * @return The indicator series and data range; `valid == false` when nothing
     *         could be computed
     */
    SeriesPlot ComputeTrend(const std::vector<Bar>& bars,
                            const std::string& name,
                            const std::vector<double>& params);

} // namespace zplot
