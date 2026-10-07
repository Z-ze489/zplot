// SPDX-License-Identifier: MIT
/**
 * @file ZeVersion.h
 * @brief Library version. Kept in sync with CMakeLists.txt by a build-time check.
 */

#pragma once

#define ZPLOT_VERSION_MAJOR  1   ///< Major -- bumped on a breaking change.
#define ZPLOT_VERSION_MINOR  0   ///< Minor -- bumped on added functionality.
#define ZPLOT_VERSION_PATCH  0   ///< Patch -- bumped on fixes only.

/// Version as text, e.g. "1.0.0".
#define ZPLOT_VERSION_STRING "1.0.0"

/// Version as one number, for compile-time comparisons:
/// major * 10000 + minor * 100 + patch.
#define ZPLOT_VERSION_NUMBER \
    (ZPLOT_VERSION_MAJOR * 10000 + ZPLOT_VERSION_MINOR * 100 + ZPLOT_VERSION_PATCH)

namespace zplot {

/**
 * @brief The version string, for a caller that would rather print it than
 *        include this header for the macro alone.
 * @return A static string such as "1.0.0"; never null.
 */
constexpr const char* VersionString() noexcept {
    return ZPLOT_VERSION_STRING;
}

}  // namespace zplot
