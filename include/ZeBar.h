/**
 * @file ZeBar.h
 * @brief A single OHLC candlestick, the smallest data unit of a chart.
 */

#pragma once

#include <string>
#include <vector>

namespace zplot {

    /**
     * @brief One candlestick.
     *
     * Prices are stored as double and converted to float pixels only when vertices
     * are generated.
     */
    struct Bar {
        int         index = 0;            ///< Horizontal plot coordinate (0-based)
        std::string time;                 ///< Time label
        double      open = 0.0;           ///< Open price
        double      high = 0.0;           ///< High price
        double      low = 0.0;            ///< Low price
        double      close = 0.0;          ///< Close price
        double      volume = 0.0;         ///< Volume (lots)
        double      openInterest = 0.0;   ///< Open interest (lots)
        double      settle = 0.0;         ///< Settlement price (falls back to close)
    };

} // namespace zplot
