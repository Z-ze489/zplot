// SPDX-License-Identifier: MIT
/**
 * @file ZeVolumeIndicator.h
 * @brief Volume and open-interest indicators.
 */

#pragma once

#include "ZeBar.h"
#include "ZePlotGeometry.h"

#include <string>
#include <vector>

namespace zplot {

    /**
     * @brief Compute a volume / open-interest indicator.
     *
     * @param name Indicator name: `CJL` / `MV` / `CCL` / `OPI` / `OBV` / `VR` /
     *             `AD` / `PVT` / `WAD` / `WVAD` / `VOSC` / `VROC` / `VRSI` /
     *             `价量运行趋势`
     * @param params Indicator parameters (order depends on name)
     * @return Several series plus the data ranges; `valid == false` when nothing
     *         could be computed
     */
    SeriesPlot ComputeVolume(const std::vector<Bar>& bars,
                             const std::string& name,
                             const std::vector<double>& params);

} // namespace zplot
