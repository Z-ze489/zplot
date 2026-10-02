/**
 * @file color4f.h
 * @brief RGBA floating-point color used across zplot.
 */

#pragma once

namespace zplot {

    /**
     * @brief RGBA color with normalized float channels (0.0 ~ 1.0).
     *
     * Default construction yields opaque black (0, 0, 0, 1).
     */
    struct Color4f {
        float r = 0.0f;   ///< Red channel (0.0 ~ 1.0)
        float g = 0.0f;   ///< Green channel (0.0 ~ 1.0)
        float b = 0.0f;   ///< Blue channel (0.0 ~ 1.0)
        float a = 1.0f;   ///< Opacity (0.0 = transparent, 1.0 = opaque)

        constexpr Color4f() = default;

        constexpr Color4f(float r_, float g_, float b_, float a_ = 1.0f)
            : r(r_), g(g_), b(b_), a(a_) {}

        /// @brief Construct from 0~255 channels (normalized internally).
        static constexpr Color4f FromRgb(unsigned char r, unsigned char g,
                                        unsigned char b, unsigned char a = 255) {
            return Color4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        }

        /// @brief Construct from a 0xRRGGBB integer.
        static constexpr Color4f FromHex(unsigned int rgb, float a = 1.0f) {
            return Color4f(((rgb >> 16) & 0xFFu) / 255.0f,
                ((rgb >> 8) & 0xFFu) / 255.0f,
                (rgb & 0xFFu) / 255.0f,
                a);
        }

        /// @brief Parse "#RRGGBB", "RRGGBB", "#RRGGBBAA" or "RRGGBBAA".
        static constexpr Color4f FromHexStr(const char* hex) {
            if (!hex || hex[0] == '\0') return Color4f();
            int i = (hex[0] == '#') ? 1 : 0;
            unsigned int v = 0;
            int n = 0;
            while (hex[i] != '\0' && n < 8) {
                v <<= 4;
                const char c = hex[i];
                v |= (c >= '0' && c <= '9') ? static_cast<unsigned int>(c - '0')
                    : (c >= 'a' && c <= 'f') ? static_cast<unsigned int>(c - 'a' + 10)
                    : (c >= 'A' && c <= 'F') ? static_cast<unsigned int>(c - 'A' + 10)
                    : 0u;
                ++i; ++n;
            }
            if (n == 6) {
                return Color4f(((v >> 16) & 0xFFu) / 255.0f,
                    ((v >> 8) & 0xFFu) / 255.0f,
                    (v & 0xFFu) / 255.0f,
                    1.0f);
            }
            if (n == 8) {
                return Color4f(((v >> 24) & 0xFFu) / 255.0f,
                    ((v >> 16) & 0xFFu) / 255.0f,
                    ((v >> 8) & 0xFFu) / 255.0f,
                    (v & 0xFFu) / 255.0f);
            }
            return Color4f();
        }

        constexpr bool operator==(const Color4f& o) const {
            return r == o.r && g == o.g && b == o.b && a == o.a;
        }

        constexpr bool operator!=(const Color4f& o) const {
            return !(*this == o);
        }

        static constexpr Color4f White() { return Color4f(1.0f, 1.0f, 1.0f, 1.0f); }
        static constexpr Color4f Black() { return Color4f(0.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Red() { return Color4f(1.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Green() { return Color4f(0.0f, 1.0f, 0.0f, 1.0f); }
        static constexpr Color4f Blue() { return Color4f(0.0f, 0.0f, 1.0f, 1.0f); }
        static constexpr Color4f Transparent() { return Color4f(0.0f, 0.0f, 0.0f, 0.0f); }
    };

} // namespace zplot
