# Changelog

All notable changes to zplot are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
The version number lives in `CMakeLists.txt` and `include/ZeVersion.h`, and a
configure-time check keeps the two in step.

## [1.0.0] - 2026-10-08

First tagged release. The API is stable; a breaking change will come with a
major version bump.

### Added

- **Candlestick geometry, four styles** — `AppendKLine` (candles, hollow up /
  solid down), `AppendBamboo` (OHLC bars), `AppendCloseLine`, `AppendTower`.
- **59 technical indicators** in three families:
  - 24 oscillators — `MACD` `KDJ` `KD` `ROC` `RSI` `SLOWKD` `WR` `BIAS` `CR`
    `ATR` `DMI` `CCI` `PSY` `MTM` `DDI` `DMA` `ADTM` `ARBR` `LON` `SRDM`
    `SHORT` `MI` `DPO` `ASl`
  - 21 trend — `MA` `EMA` `EMA2` `SMA` `TRMA` `TSMA` `BOLL` `BBlBOLL`
    `MA扩展` `PUBU` `SAR` `SAR1` `HCL` `MIKE` `BBI` `DKX` `CDP` `唐奇安`
    `ENV` `SP` `空`
  - 14 volume / open-interest — `CJL` `MV` `CCL` `OPI` `OBV` `VR` `AD`
    `PVT` `WAD` `WVAD` `VOSC` `VROC` `VRSI` `价量运行趋势`
- **Geometry helpers** — viewport mapping, plus triangulation of rectangles,
  thick segments, polylines, bars and scatter points, usable for custom
  overlays without touching the indicator code.
- **Version header** — `include/ZeVersion.h`, with `ZPLOT_VERSION_*` macros and
  `zplot::VersionString()`.
- **Demo** — a self-contained Win32 + OpenGL 4.6 viewer that builds and runs
  from a fresh clone; the only third-party code is a vendored glad loader.
- **Documentation** — a developer guide in English and Chinese, five sample
  programs that `verify-examples.ps1` compiles and runs, and an indicator
  reference listing formulas, default parameters and the cross-check results
  against WenHua wh6 and TDX.

### Notes

- The library links nothing but the C++ standard library and never calls a
  graphics API: input is a `std::vector<zplot::Bar>`, output is a flat list of
  triangle vertices.
- Default palettes follow the Chinese convention (red up, cyan down);
  `DimForLightBackground()` adapts a plot to a light canvas, and `KLineStyle`
  can be overridden for the western convention.
- The CTP trading wrapper is **not** part of this repository; it is distributed
  separately. See [CONTACT.md](CONTACT.md).

[1.0.0]: https://github.com/Z-ze489/zplot/releases/tag/v1.0.0
