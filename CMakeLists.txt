cmake_minimum_required(VERSION 3.20)
project(zplot LANGUAGES C CXX)

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
