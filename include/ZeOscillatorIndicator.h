// SPDX-License-Identifier: MIT
/**
 * @file ZeOscillatorIndicator.h
 * @brief Oscillator (sub-chart) indicators.
 */

#pragma once

#include "ZeBar.h"
#include "ZePlotGeometry.h"

#include <string>
#include <vector>

namespace zplot {

    /**
     * @brief Compute an oscillator indicator.
     *
     * @param name Indicator name: `MACD` / `KDJ` / `KD` / `ROC` / `RSI` / `SLOWKD` /
     *             `WR` / `BIAS` / `CR` / `ATR` / `DMI` / `CCI` / `PSY` / `MTM` /
     *             `DDI` / `DMA` / `ADTM` / `ARBR` / `LON` / `SRDM` / `SHORT` / `MI` /
     *             `DPO` / `ASl`
     * @param params Indicator parameters (order depends on name)
     * @return Several series plus the data ranges; `valid == false` when nothing
     *         could be computed (unknown name, too few parameters, or no data)
     */
    SeriesPlot ComputeOscillator(const std::vector<Bar>& bars,
                                 const std::string& name,
                                 const std::vector<double>& params);

} // namespace zplot
