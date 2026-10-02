/**
 * @file ZeTextRenderer.h
 * @brief Bitmap-font text rendering for the demo: a GDI-built glyph atlas + quads.
 *
 * @details
 * The charts need labels (indicator name, parameter list, last values, hints), but
 * pulling in FreeType just for that would defeat the point of a self-contained demo.
 * Instead the atlas is painted once at start-up with GDI -- the Win32 API we already
 * link against -- and then sampled like any other texture.
 *
 * How it works:
 *
 *  1. a 32bpp DIB section is created and an ASCII range (32..126) is painted into a
 *     grid of cells, white on black, with grayscale antialiasing;
 *  2. the red channel of that DIB becomes an `GL_R8` coverage texture (one byte per
 *     pixel, 0 = transparent, 255 = fully covered);
 *  3. drawing a string emits one `cellW x cellH` quad per character, advancing the pen
 *     by the character's real advance width. Because each glyph keeps the exact offset
 *     it had inside its cell, the result matches what GDI painted.
 *
 * @note The atlas covers ASCII 32..126 only. Bytes >= 0x80 are replaced with '?'.
 */

#pragma once

#include "ZeOpenGL.h"
#include "color4f.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace zegl {

    /// @brief One text vertex: pixel position, atlas UV and color.
    struct TextVertex {
        float x, y;          ///< Pixel position (origin top-left, Y down)
        float u, v;          ///< Atlas texture coordinates
        float r, g, b, a;    ///< Color
    };

    /**
     * @brief One glyph atlas for a single font size and weight.
     */
    class TextAtlas {
    public:
        static constexpr int kFirst = 32;             ///< First character in the atlas (' ')
        static constexpr int kLast = 126;             ///< Last character in the atlas ('~')
        static constexpr int kCount = kLast - kFirst + 1;
        static constexpr int kCols = 16;
        static constexpr int kRows = (kCount + kCols - 1) / kCols;   // 6
        static constexpr int kCellW = 32;             ///< Cell width in the atlas (pixels)
        static constexpr int kCellH = 32;             ///< Cell height in the atlas (pixels)

        TextAtlas() = default;
        ~TextAtlas() { Destroy(); }
        TextAtlas(const TextAtlas&) = delete;
        TextAtlas& operator=(const TextAtlas&) = delete;

        /**
         * @brief Paint the atlas and upload it as a texture.
         * @param pixelSize Font height in pixels (character height)
         * @param bold      Render a bold face
         * @param face      Preferred typeface; GDI substitutes if it is unavailable
         * @param err       Filled with a reason on failure
         * @return Whether the atlas is ready for use
         */
        bool Build(int pixelSize, bool bold, const char* face, std::string& err) {
            Destroy();

            const int texW = kCols * kCellW;
            const int texH = kRows * kCellH;

            // ---- a top-down 32bpp DIB we can paint into and read back ---------
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = texW;
            bi.bmiHeader.biHeight = -texH;          // negative = top-down rows
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            HDC screen = ::GetDC(nullptr);
            HDC mem = ::CreateCompatibleDC(screen);

            void* bits = nullptr;
            HBITMAP dib = ::CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!dib || !bits) {
                err = "CreateDIBSection failed";
                ::DeleteDC(mem); ::ReleaseDC(nullptr, screen);
                return false;
            }
            HGDIOBJ oldBmp = ::SelectObject(mem, dib);

            // Black background; white glyphs with grayscale antialiasing give us a
            // clean coverage mask in the red channel.
            RECT all{ 0, 0, texW, texH };
            ::FillRect(mem, &all, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
            ::SetBkMode(mem, TRANSPARENT);
            ::SetTextColor(mem, RGB(255, 255, 255));

            HFONT font = ::CreateFontA(-pixelSize, 0, 0, 0,
                bold ? FW_BOLD : FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
            if (!font) font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
            HGDIOBJ oldFont = ::SelectObject(mem, font);

            TEXTMETRICA tm{};
            ::GetTextMetricsA(mem, &tm);
            lineHeight_ = static_cast<float>(tm.tmHeight);

            for (int i = 0; i < kCount; ++i) {
                const char ch = static_cast<char>(kFirst + i);
                const int col = i % kCols;
                const int row = i / kCols;

                // TextOut takes the top-left of the character cell as its origin, and
                // the quad we emit later uses the very same cell -- so the glyph's
                // offset inside the cell is preserved.
                ::TextOutA(mem, col * kCellW, row * kCellH, &ch, 1);

                SIZE sz{};
                ::GetTextExtentPoint32A(mem, &ch, 1, &sz);
                advance_[i] = static_cast<float>(sz.cx);
            }

            ::GdiFlush();

            // ---- pack the red channel into a single-byte coverage buffer ------
            std::vector<unsigned char> cov(static_cast<std::size_t>(texW) * texH);
            const unsigned char* px = static_cast<const unsigned char*>(bits);
            for (int y = 0; y < texH; ++y) {
                for (int x = 0; x < texW; ++x) {
                    // BGRA layout: byte 0 is blue, 1 green, 2 red
                    cov[static_cast<std::size_t>(y) * texW + x] = px[(static_cast<std::size_t>(y) * texW + x) * 4 + 2];
                }
            }

            // ---- upload ------------------------------------------------------
            glGenTextures(1, &tex_);
            glBindTexture(GL_TEXTURE_2D, tex_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texW, texH, 0,
                GL_RED, GL_UNSIGNED_BYTE, cov.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            // ---- clean up GDI ------------------------------------------------
            ::SelectObject(mem, oldFont);
            ::DeleteObject(font);
            ::SelectObject(mem, oldBmp);
            ::DeleteObject(dib);
            ::DeleteDC(mem);
            ::ReleaseDC(nullptr, screen);

            valid_ = true;
            return true;
        }

        void Destroy() {
            if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
            valid_ = false;
        }

        GLuint texture() const { return tex_; }
        bool   valid() const { return valid_; }

        /// @brief Line height in pixels (the font's character height).
        float lineHeight() const { return lineHeight_; }

        /// @brief Advance width of a string, in pixels.
        float Measure(const char* text) const {
            float w = 0.0f;
            for (const char* p = text; *p; ++p) w += advance_[Index(*p)];
            return w;
        }

        /**
         * @brief Emit quads for `text` starting at (x, y), where y is the **top edge**.
         * @return The pen x position after the last glyph.
         */
        float Draw(std::vector<TextVertex>& out, float x, float y,
            const char* text, const zplot::Color4f& c) const {

            const float texW = static_cast<float>(kCols * kCellW);
            const float texH = static_cast<float>(kRows * kCellH);

            float pen = x;
            for (const char* p = text; *p; ++p) {
                const int i = Index(*p);
                if (i < 0) continue;

                const int col = i % kCols;
                const int row = i / kCols;

                // Sample half a texel inside the cell so neighbouring glyphs never bleed in.
                const float u0 = (col * kCellW + 0.5f) / texW;
                const float v0 = (row * kCellH + 0.5f) / texH;
                const float u1 = (col * kCellW + kCellW - 0.5f) / texW;
                const float v1 = (row * kCellH + kCellH - 0.5f) / texH;

                const float x0 = pen, x1 = pen + kCellW;
                const float y0 = y, y1 = y + kCellH;

                auto vtx = [&](float px_, float py_, float u_, float v_) {
                    out.push_back(TextVertex{ px_, py_, u_, v_, c.r, c.g, c.b, c.a });
                };
                vtx(x0, y0, u0, v0);
                vtx(x1, y0, u1, v0);
                vtx(x1, y1, u1, v1);
                vtx(x0, y0, u0, v0);
                vtx(x1, y1, u1, v1);
                vtx(x0, y1, u0, v1);

                pen += advance_[i];
            }
            return pen;
        }

    private:
        /// @brief Map a character to an atlas index, or -1 to skip it.
        static int Index(char ch) {
            const unsigned char u = static_cast<unsigned char>(ch);
            if (u < static_cast<unsigned char>(kFirst) || u > static_cast<unsigned char>(kLast)) {
                // Anything outside ASCII lands on '?' unless it is a control character.
                return (u >= 0x80u) ? ('?' - kFirst) : -1;
            }
            return static_cast<int>(u) - kFirst;
        }

        GLuint tex_ = 0;
        bool   valid_ = false;
        float  lineHeight_ = 0.0f;
        float  advance_[kCount] = {};
    };

} // namespace zegl
