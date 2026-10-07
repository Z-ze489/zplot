cmake_minimum_required(VERSION 3.20)

project(zplot
    VERSION     1.0.0
    DESCRIPTION "OHLC bars in, triangle vertices out -- a dependency-free C++20 charting library"
    LANGUAGES   C CXX
)

# Keep the header in step with the version above. A consumer can read the
# number from either place, so a silent mismatch would be worse than a build
# that stops -- bump both together.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/ZeVersion.h" _zplot_version_header)
if(NOT _zplot_version_header MATCHES "ZPLOT_VERSION_STRING \"${PROJECT_VERSION}\"")
    message(FATAL_ERROR
        "include/ZeVersion.h does not match project VERSION (${PROJECT_VERSION}). "
        "Set ZPLOT_VERSION_STRING to \"${PROJECT_VERSION}\" in that header.")
endif()

# ---------------------------------------------------------------------------
# zplot -- static library: candlesticks, oscillators, trend and volume studies.
# ---------------------------------------------------------------------------
add_library(zplot STATIC
    src/ZeKLineChart.cpp
    src/ZeOscillatorIndicator.cpp
    src/ZeTrendIndicator.cpp
    src/ZeVolumeIndicator.cpp
)
target_include_directories(zplot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(zplot PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(zplot PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# demo -- Win32 window + OpenGL 4.6 viewer.
# ---------------------------------------------------------------------------
option(ZPLOT_BUILD_DEMO "Build the Win32 + OpenGL demo" ON)

if(ZPLOT_BUILD_DEMO)
    add_executable(zplot_demo
        demo/main.cpp
        demo/glad/src/glad.c
    )
    target_link_libraries(zplot_demo PRIVATE
        zplot
        opengl32 gdi32 user32 kernel32 shell32
    )
    target_include_directories(zplot_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demo/glad/include)
    target_compile_features(zplot_demo PRIVATE cxx_std_20)
    set_source_files_properties(demo/glad/src/glad.c PROPERTIES LANGUAGE C)

    if(MSVC)
        target_compile_options(zplot_demo PRIVATE /utf-8)
    endif()
endif()
