// SPDX-License-Identifier: MIT
/**
 * @file ZeOpenGL.h
 * @brief Win32 + WGL window bootstrap: create an OpenGL 4.6 core-profile context.
 *
 * @details
 * The GL entry points themselves come from **glad** (vendored under `glad/`, generated
 * for `gl=4.6 core` with the loader enabled). Once `ZeGLCreateContext()` returns, the
 * whole 4.6 API is live -- just call `glDrawArrays()` and friends.
 *
 * What this file *does* cover is the one thing glad cannot do: **WGL cannot create a
 * 3.2+ context directly.** `wglChoosePixelFormatARB` and `wglCreateContextAttribsARB`
 * are only exposed in the driver once *some* GL context is current, so a throwaway
 * window and a legacy context have to come first:
 *
 *   1. create a temporary window and a legacy context on it
 *   2. fetch the two WGL extension entry points through that context
 *   3. destroy the temporary context
 *   4. create the real 4.6 core context on the caller's window
 *   5. `gladLoadGL()` -- now every GL call in the program resolves
 *
 * Shader compilation helpers live here too; they are glue, not a loader.
 */

#pragma once

// windows.h defines min/max as macros, which would break std::min / std::max.
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <glad/glad.h>

#include <cstdio>
#include <string>

namespace zegl {

    // -----------------------------------------------------------------------
    //  WGL extension entry points
    // -----------------------------------------------------------------------
    //  glad is generated for the GL spec only, so WGL_ARB_pixel_format /
    //  WGL_ARB_create_context still have to be fetched by hand.

    typedef BOOL(WINAPI* PFN_wglChoosePixelFormatARB)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

    inline PFN_wglChoosePixelFormatARB    wglChoosePixelFormatARB = nullptr;
    inline PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB = nullptr;

    // WGL_ARB_pixel_format tokens
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_ALPHA_BITS_ARB                0x201B
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_STENCIL_BITS_ARB              0x2023
#define WGL_ACCELERATION_ARB              0x2003
#define WGL_FULL_ACCELERATION_ARB         0x2027
#define WGL_SAMPLE_BUFFERS_ARB            0x2041
#define WGL_SAMPLES_ARB                   0x2042
#define WGL_TYPE_RGBA_ARB                 0x202B

    // WGL_ARB_create_context tokens
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x0001

    // -----------------------------------------------------------------------
    //  Context creation
    // -----------------------------------------------------------------------

    /// @brief Requested context properties.
    struct GLConfig {
        int  major = 4;      ///< Requested major version
        int  minor = 6;      ///< Requested minor version
        int  samples = 4;    ///< MSAA sample count; 0 disables multisampling
        bool core = true;    ///< Core profile (false = compatibility profile)
    };

    inline LRESULT CALLBACK ZeGLTempProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        return ::DefWindowProcA(h, m, w, l);
    }

    /// @brief Resolve one WGL extension entry point with the sentinel checks glad does not do.
    inline PROC ZeGLGetWglProc(const char* name) {
        PROC p = ::wglGetProcAddress(name);
        if (p && p != reinterpret_cast<PROC>(1) && p != reinterpret_cast<PROC>(2) &&
            p != reinterpret_cast<PROC>(3) && p != reinterpret_cast<PROC>(-1)) {
            return p;
        }
        return nullptr;
    }

    /**
     * @brief Create an OpenGL 4.6 core context on `hwnd`, make it current, and load glad.
     *
     * @param hwnd Window to render into (it must not have had SetPixelFormat called on it)
     * @param cfg  Requested version / profile / MSAA
     * @param err  Filled with a human-readable reason on failure
     * @return The created context, or nullptr on failure
     *
     * @note MSAA is best-effort: if no multisampled pixel format is available the
     *       context is created without it rather than failing outright.
     */
    inline HGLRC ZeGLCreateContext(HWND hwnd, const GLConfig& cfg, std::string& err) {
        // ---- step 1: a throwaway window + a legacy context -------------------
        const char* kTempClass = "ZeGLBootstrapWindow";

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ZeGLTempProc;
        wc.hInstance = ::GetModuleHandleA(nullptr);
        wc.lpszClassName = kTempClass;
        ::RegisterClassExA(&wc);   // harmless if it is already registered

        HWND dummy = ::CreateWindowExA(0, kTempClass, "", WS_OVERLAPPEDWINDOW,
            0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        if (!dummy) { err = "CreateWindowExA failed for the bootstrap window"; return nullptr; }

        HDC dummyDC = ::GetDC(dummy);

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;

        const int dummyFmt = ::ChoosePixelFormat(dummyDC, &pfd);
        if (!dummyFmt || !::SetPixelFormat(dummyDC, dummyFmt, &pfd)) {
            err = "SetPixelFormat failed on the bootstrap window";
            ::ReleaseDC(dummy, dummyDC); ::DestroyWindow(dummy);
            return nullptr;
        }

        HGLRC dummyCtx = ::wglCreateContext(dummyDC);
        if (!dummyCtx || !::wglMakeCurrent(dummyDC, dummyCtx)) {
            err = "wglCreateContext failed on the bootstrap window";
            if (dummyCtx) ::wglDeleteContext(dummyCtx);
            ::ReleaseDC(dummy, dummyDC); ::DestroyWindow(dummy);
            return nullptr;
        }

        wglChoosePixelFormatARB = reinterpret_cast<PFN_wglChoosePixelFormatARB>(
            ZeGLGetWglProc("wglChoosePixelFormatARB"));
        wglCreateContextAttribsARB = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
            ZeGLGetWglProc("wglCreateContextAttribsARB"));

        ::wglMakeCurrent(nullptr, nullptr);
        ::wglDeleteContext(dummyCtx);
        ::ReleaseDC(dummy, dummyDC);
        ::DestroyWindow(dummy);

        if (!wglChoosePixelFormatARB || !wglCreateContextAttribsARB) {
            err = "required WGL extensions (ARB_pixel_format / ARB_create_context) are missing; "
                  "the graphics driver is too old for OpenGL 4.6";
            return nullptr;
        }

        // ---- step 2: the real pixel format on the caller's window ------------
        HDC dc = ::GetDC(hwnd);
        if (!dc) { err = "GetDC failed"; return nullptr; }

        int pixelFormat = 0;
        int chosenSamples = 0;
        UINT numFormats = 0;

        auto tryFormat = [&](int samples) -> bool {
            int attribs[] = {
                WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
                WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
                WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB,     32,
                WGL_ALPHA_BITS_ARB,     8,
                WGL_DEPTH_BITS_ARB,     24,
                WGL_STENCIL_BITS_ARB,   8,
                WGL_SAMPLE_BUFFERS_ARB, samples > 0 ? 1 : 0,
                WGL_SAMPLES_ARB,        samples > 0 ? samples : 0,
                0
            };
            pixelFormat = 0;
            numFormats = 0;
            if (!wglChoosePixelFormatARB(dc, attribs, nullptr, 1, &pixelFormat, &numFormats)
                || numFormats == 0 || pixelFormat == 0) {
                return false;
            }
            chosenSamples = samples;
            return true;
        };

        if (!tryFormat(cfg.samples) && !tryFormat(0)) {
            err = "wglChoosePixelFormatARB found no usable pixel format";
            ::ReleaseDC(hwnd, dc);
            return nullptr;
        }

        PIXELFORMATDESCRIPTOR chosen{};
        ::DescribePixelFormat(dc, pixelFormat, sizeof(chosen), &chosen);
        if (!::SetPixelFormat(dc, pixelFormat, &chosen)) {
            err = "SetPixelFormat failed on the target window";
            ::ReleaseDC(hwnd, dc);
            return nullptr;
        }

        const bool msaa = chosenSamples > 0;

        // ---- step 3: the modern context -------------------------------------
        const int profile = cfg.core ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB : 0;

        const int ctxAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, cfg.major,
            WGL_CONTEXT_MINOR_VERSION_ARB, cfg.minor,
            WGL_CONTEXT_PROFILE_MASK_ARB,  profile,
            WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
            0
        };

        HGLRC ctx = wglCreateContextAttribsARB(dc, nullptr, ctxAttribs);

        if (!ctx) {
            // Some drivers refuse a forward-compatible core context; drop the flags
            // and try once more before giving up.
            const int fallback[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, cfg.major,
                WGL_CONTEXT_MINOR_VERSION_ARB, cfg.minor,
                WGL_CONTEXT_PROFILE_MASK_ARB,  profile,
                0
            };
            ctx = wglCreateContextAttribsARB(dc, nullptr, fallback);
        }

        if (!ctx) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "the driver refused an OpenGL %d.%d %s-profile context",
                cfg.major, cfg.minor, cfg.core ? "core" : "compatibility");
            err = buf;
            ::ReleaseDC(hwnd, dc);
            return nullptr;
        }

        if (!::wglMakeCurrent(dc, ctx)) {
            err = "wglMakeCurrent failed";
            ::wglDeleteContext(ctx);
            ::ReleaseDC(hwnd, dc);
            return nullptr;
        }

        // ---- step 4: hand over to glad --------------------------------------
        if (!gladLoadGL()) {
            err = "gladLoadGL failed -- could not resolve the OpenGL entry points";
            return nullptr;
        }

        if (msaa) glEnable(GL_MULTISAMPLE);
        return ctx;
    }

    /// @brief Swap buffers on `dc`.
    inline void ZeGLSwapBuffers(HDC dc) { ::SwapBuffers(dc); }

    // -----------------------------------------------------------------------
    //  Shader helpers
    // -----------------------------------------------------------------------

    inline bool ZeGLCompileStage(GLenum type, const char* src, GLuint& out, std::string& err) {
        out = glCreateShader(type);
        glShaderSource(out, 1, &src, nullptr);
        glCompileShader(out);

        GLint ok = GL_FALSE;
        glGetShaderiv(out, GL_COMPILE_STATUS, &ok);
        if (ok) return true;

        GLint len = 0;
        glGetShaderiv(out, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(out, len, nullptr, log.data());
        err = (type == GL_VERTEX_SHADER ? "vertex shader: " : "fragment shader: ") + log;
        glDeleteShader(out);
        out = 0;
        return false;
    }

    /**
     * @brief Compile and link a vertex / fragment pair.
     * @param err Filled with the compiler or linker log on failure
     * @return The program object, or 0 on failure
     */
    inline GLuint ZeGLBuildProgram(const char* vs, const char* fs, std::string& err) {
        GLuint v = 0, f = 0;
        if (!ZeGLCompileStage(GL_VERTEX_SHADER, vs, v, err)) return 0;
        if (!ZeGLCompileStage(GL_FRAGMENT_SHADER, fs, f, err)) { glDeleteShader(v); return 0; }

        const GLuint p = glCreateProgram();
        glAttachShader(p, v);
        glAttachShader(p, f);
        glLinkProgram(p);
        glDeleteShader(v);
        glDeleteShader(f);

        GLint ok = GL_FALSE;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
            std::string log(static_cast<std::size_t>(len > 1 ? len : 1), '\0');
            glGetProgramInfoLog(p, len, nullptr, log.data());
            err = "link: " + log;
            glDeleteProgram(p);
            return 0;
        }
        return p;
    }

} // namespace zegl
