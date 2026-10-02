/**
 * @file ZePlotTypes.h
 * @brief Plotting data contract: PlotVertex (a vertex) and PlotText (a text label).
 */

#pragma once

#include "color4f.h"

#include <cstddef>
#include <string>

namespace zplot {

    /**
     * @brief One plotting vertex: position plus color.
     *
     * Vertices form triangles back to back, so no index buffer is needed.
     */
    struct PlotVertex {
        float x = 0.0f;   ///< X coordinate (pixel, origin at top-left)
        float y = 0.0f;   ///< Y coordinate (pixel, Y grows downward)
        float r = 0.0f;   ///< Red channel (0.0 ~ 1.0)
        float g = 0.0f;   ///< Green channel (0.0 ~ 1.0)
        float b = 0.0f;   ///< Blue channel (0.0 ~ 1.0)
        float a = 1.0f;   ///< Opacity (0.0 = transparent, 1.0 = opaque)
    };

    static_assert(sizeof(PlotVertex) == 6 * sizeof(float),
        "PlotVertex must be exactly 6 tightly packed floats");

    /**
     * @brief Horizontal alignment of a text anchor relative to its x coordinate.
     */
    enum class PlotTextAlign {
        Left,     ///< x is the left edge of the text
        Center,   ///< x is the horizontal center of the text
        Right,    ///< x is the right edge of the text
    };

    /**
     * @brief One piece of annotation text.
     */
    struct PlotText {
        std::string   text;                            ///< UTF-8 text, may contain '\n'
        float         x = 0.0f;                        ///< Anchor x (meaning depends on align)
        float         y = 0.0f;                        ///< Top edge of the text
        unsigned      pixelSize = 12;                  ///< Font size in pixels
        bool          bold = false;                    ///< Bold flag
        PlotTextAlign align = PlotTextAlign::Left;     ///< Which edge x refers to
        Color4f       color = Color4f(0.0f, 0.0f, 0.0f, 1.0f);   ///< Text color
    };

} // namespace zplot
