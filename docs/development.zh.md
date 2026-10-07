# 开发者使用指南

> English: [development.md](development.md) ·
> 文档总目录：[目录.md](目录.md)

把 zplot 接进你自己的程序。覆盖完整链路：编译库、喂数据、算指标、转顶点、
交给任意渲染器。

本文引用的每一段代码都是 [`docs/examples/`](examples/) 下的真实文件，**在写进
文档之前已经编译并运行过**。每节展示的输出都是实测结果，不是手写的。

---

## 一、你拿到什么，不拿到什么

| | |
|---|---|
| **库内提供** | K 线几何（5 种样式）、59 个技术指标、视口映射、三角形剖分工具（`include/`、`src/`） |
| **库内没有** | 任何渲染器、任何窗口、任何字体、任何文件 / 网络 IO |
| **你需要提供** | 一个 `std::vector<Bar>`、一块可绘制的地方、一个关窗的方式 |

zplot 不碰任何图形 API，也不打开任何文件。它是一个纯函数库：数据进，
三角形出。所以它除了 C++ 标准库之外什么都不链接。

## 二、编译

```bash
cmake -B build
cmake --build build --config Release
```

要求：Windows x64、CMake 3.20+、C++20 编译器（开发环境为 MSVC 工具集 v145）。
库本身**无**第三方依赖；demo 内嵌 `glad` 用于加载 GL 入口点。

只想在自己的工程里用库，直接把源码加进来即可 —— 没有任何东西需要链接：

```cmake
add_subdirectory(zplot)
target_link_libraries(your_app PRIVATE zplot)
```

## 三、数据契约

一切从 `std::vector<zplot::Bar>` 开始（[`include/ZeBar.h`](../include/ZeBar.h)）：

```cpp
struct Bar {
    int         index = 0;          // 横向绘图坐标，从 0 开始
    std::string time;               // 时间标签
    double      open, high, low, close;
    double      volume;             // 成交量（手）
    double      openInterest;       // 持仓量（手）
    double      settle;             // 结算价，为 0 时回退到 close
};
```

两条硬规则：

- **`index` 必须从 0 开始且连续。** 横轴是按 `index` 生成的，不是按数组下标。
  `index` 有断层，图上就有断层；起始值不是 0，所有面板都会整体偏移。
- **`index` 必须递增。** 库不会替你检查顺序。

## 四、两个阶段：先算，再画

所有指标都是同一个形状，理解这个原因能避免大部分错误：**必须先知道数值范围，
才能配置视口**。所以指标计算一次，消费两次。

```
Compute*()           ->  SeriesPlot   数据坐标的点 + 取值范围
Viewport             <-  用那个范围配出来
Append*/AppendSeries ->  PlotVertex[] 像素坐标，三角面片
```

### 快速上手

示例文件：[`examples/01_quickstart.cpp`](examples/01_quickstart.cpp)

```cpp
#include <ZeBar.h>
#include <ZeKLineChart.h>
#include <ZeOscillatorIndicator.h>
#include <ZePlotGeometry.h>

std::vector<zplot::Bar> bars = LoadYourBars();   // index 必须从 0 开始

// 1. 算一次 —— 数据坐标的序列 + 取值范围
zplot::SeriesPlot macd = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
if (!macd.valid) { /* 名字未知、参数太少，或数据不够 */ }

// 2. 用这个范围配置视口
zplot::Viewport vp;
vp.left = 0.0f;   vp.top = 0.0f;
vp.width = 800.0f; vp.height = 300.0f;
vp.xMin = 7600.0f; vp.xMax = 7999.0f;
vp.yMin = macd.yMin; vp.yMax = macd.yMax;

// 3. 出三角形
std::vector<zplot::PlotVertex> verts;
zplot::AppendSeries(verts, macd, vp);

zplot::Viewport priceVp = zplot::FitViewport(bars, 0.0f, 0.0f, 800.0f, 500.0f, 400);
zplot::KLineStyle style;
zplot::AppendKLine(verts, bars, priceVp, style);
```

8000 根数据的实测输出：

```
vertices           : 244056
triangles          : 81352
bytes to upload    : 5857344
vertex layout      : 6 floats (24 bytes)
series in the plot : 5
```

## 五、指标

三个族，各一个入口：

```cpp
zplot::SeriesPlot p1 = zplot::ComputeTrend     (bars, "MA",   { 5, 10, 20, 30 });
zplot::SeriesPlot p2 = zplot::ComputeOscillator(bars, "MACD", { 12, 26, 9 });
zplot::SeriesPlot p3 = zplot::ComputeVolume    (bars, "CJL",  { 5, 10 });
```

全部名字、公式与交易软件对账结果见 [indicators.md](indicators.md) /
[indicators.zh.md](indicators.zh.md)。

### 失败是静默的

没有异常，也没有错误码。无法满足的调用会返回 `valid == false` 的 `SeriesPlot`：

```cpp
const zplot::SeriesPlot bad = zplot::ComputeOscillator(bars, "NOT_A_NAME", { 1 });
// bad.valid == false
```

**读 `series` / `yMin` / `yMax` 之前，先判 `valid`。**

### 参数个数陷阱

画不出图最常见的原因。每个指标都有**最少参数个数**，给少了（其中有一个指标是
**给了就错**）会静默失败。由
[`examples/02_indicators.cpp`](examples/02_indicators.cpp) 实测：

```
  SLOWKD   n=3  valid=false  3 params -> needs 4 (N, M1, M2, M3)
  SLOWKD   n=4  valid=true   4 params -> ok
  PSY      n=1  valid=false  1 param  -> needs 2 (period, maPeriod)
  PSY      n=2  valid=true   2 params -> ok
  BOLL     n=2  valid=false  2 params -> needs 3 (N, M, P)
  BOLL     n=3  valid=true   3 params -> ok
  PUBU     n=3  valid=false  3 params -> needs 6
  PUBU     n=6  valid=true   6 params -> ok
  ASl      n=1  valid=false  any param -> must be empty
  ASl      n=0  valid=true   no params -> ok
```

两个需要单独说明：

- **`ASl` 不接受任何参数。** 传了反而失败。调用方式：
  `ComputeOscillator(bars, "ASl", {})`。
- **`PUBU` 要求 6 个参数但忽略其值。** 周期在内部固定为 4/6/9/13/18/24，
  这个 vector 只要够长即可。

59 个指标在同一份 400 根数据上全部算得出：

```
Oscillators      24
Trend            21
Volume / OI      14
total=59  invalid=0
```

其中 4 个的 **API 字符串是中文**，扫代码列表时容易漏，也是"看起来写全了、
实际少数几个"的原因：

```cpp
zplot::ComputeTrend (bars, "MA扩展", { 120, 240 });
zplot::ComputeTrend (bars, "唐奇安", { 20, 20 });
zplot::ComputeTrend (bars, "空",     { });          // 什么都不画，valid 仍为 true
zplot::ComputeVolume(bars, "价量运行趋势", { 5, 10 });
```

### 读回 SeriesPlot

`SeriesPlot` 是一组具名序列加取值范围：

```
MACD breakdown
  series 0: prim=Bar  points=233  width=1.5 barWidth=4.0 barBase=0
  series 1: prim=Bar  points=142  width=1.5 barWidth=4.0 barBase=0
  series 2: prim=Line points=2    width=1.5 barWidth=4.0 barBase=0
  series 3: prim=Line points=375  width=1.5 barWidth=4.0 barBase=0
  series 4: prim=Line points=375  width=1.5 barWidth=4.0 barBase=0
  range: x=[25,399] y=[-22.3036,8.80445]
```

注意 MACD 返回 **5 条**序列 —— 柱状图加 DIF/DEA 和零轴，而且柱状图用的是
`prim == Bar` 而不是 `Line`。

## 六、几何

三角形剖分工具是公开接口。可以用它们画自己的叠加层（水平线、标记、方框），
不必碰指标代码。全部示例见 [`examples/03_geometry.cpp`](examples/03_geometry.cpp)。

### 视口映射

`Viewport` 是一个线性映射。**纵轴是翻转的** —— 数值越大 `y` 越小，因为像素
`y` 是向下增长的。

```
  x(0)=0.00  x(50)=400.00  x(100)=800.00
  y(8000)=0.00  y(7000)=400.00   <- y is flipped
  dx()=8.000  dataWidth()=800.0
  degenerate: x(5)=10.0 y(5)=70.0 dx=0.0  (no NaN)
```

退化范围（`yMin == yMax`）是安全的：返回底边，`dx()` 返回 0，所以平移到一段
平坦区域也不会产生 NaN 顶点。

### 图元

| 辅助函数 | 产出 | 三角形数 |
|---|---|---|
| `PushTriangle` | 一个三角形 | 1 |
| `PushQuadPoints` | 任意四边形 | 2 |
| `PushRect` | 轴对齐矩形 | 2 |
| `PushSegment` | 一条加粗线段 | 2 |
| `PushPoint` | 一个方块（散点） | 2 |
| `PushBar` | 从基线起的柱子 | 2 |
| `PushPolyline` | 加粗折线 + 关节 | 每段 2 |
| `PushPolylineBy` | 同上，点由回调产生 | 每段 2 |

实测顶点数：

```
  PushTriangle             +3      -> 3 verts (1 tris)
  PushQuadPoints           +6      -> 9 verts (3 tris)
  PushRect                 +6      -> 15 verts (5 tris)
  PushSegment              +6      -> 21 verts (7 tris)
  PushPoint                +6      -> 27 verts (9 tris)
  PushBar                  +6      -> 33 verts (11 tris)
  PushSegment(degenerate)  +0      -> 33 verts (11 tris)
```

零长度线段贡献 **0** 个顶点，而不是退化的三角形 —— 重复点可以放心传。

### 断线

在线条该断开的地方插入 `NaN`，当前段结束，下一个有效点开始新的一段。
适合处理指标的预热期。

```cpp
const float nan = std::numeric_limits<float>::quiet_NaN();
const float xy[] = { 0, 0, 10, 10, nan, nan, 20, 20, 30, 30 };
zplot::PushPolyline(v, xy, 5, 2.0f, color);   // 两段独立的线
```

`PushPolylineBy` 按需生成点 —— 当把整个数组物化出来很浪费时有用：

```cpp
zplot::PushPolylineBy(v, 20, [](std::size_t i, float& x, float& y) {
    x = static_cast<float>(i) * 4.0f;
    y = 50.0f + 30.0f * std::sin(static_cast<float>(i) * 0.5f);
    return true;      // 返回 false 断开
}, 1.5f, color);
```

### `AppendSeries`

一次转换整个 `SeriesPlot`，按每条序列的 `SeriesPrim` 分派：

```
  AppendSeries(3 series)   +228    -> 567 verts (189 tris)
```

`AppendSeries` 是**追加**，不会清空 vector。正因如此，你可以在一次上传之前
把多个研究叠加进同一个缓冲区。

## 七、颜色、范围与自适应

示例：[`examples/04_theme_and_range.cpp`](examples/04_theme_and_range.cpp)

### 浅色背景陷阱

**默认调色板是按深色画布调的。** 画到浅色背景上，灰白线条会完全看不见。
`DimForLightBackground()` 只压暗这些，饱和色不动：

```
light-background trap
  series=4  bright(before)=1
    color=(1.000, 1.000, 1.000, 1.00)
    color=(1.000, 1.000, 0.000, 1.00)
    color=(0.500, 0.000, 1.000, 1.00)
    color=(0.000, 1.000, 0.000, 1.00)
  bright(after)=0
  vertices on a light canvas: 18444
  vertices on a dark canvas : 18444  (identical count: yes)
```

算完之后调一次，且只对浅色画布调：

```cpp
zplot::SeriesPlot ma = zplot::ComputeTrend(bars, "MA", { 5, 10, 20, 30 });
ma.DimForLightBackground();     // 深色画布下是空操作
```

它只改颜色、不改几何，所以两种情况顶点数完全一致。

### 取值范围

```cpp
plot.PadRange(0.05f);       // 上下各留 5% 余量
```

```
  MA raw          y=[7552.0366, 7654.4766]
  MA PadRange(5%) y=[7546.9146, 7659.5986]

  PriceRange      ok=true  y=[7540.96, 7668.24]
  PriceRange      ok=false (empty slice)
```

`PriceRange` 在该区间内没有数据时返回 `false` —— 用它之前先判返回值，
这种情况输出参数保持原值不动。

### `FitViewport`

从数据自动配视口，可只显示最后 *N* 根：

```
  all bars : x=[0,399]   y=[7540.96,7668.24]
  last 100 : x=[300,399] y=[7548.52,7667.88]
  empty    : x=[0,1]     y=[0.00,1.00]   (no crash)
```

空 vector 下返回安全的单位范围，不会除以零。

### K 线样式

```cpp
zplot::KLineStyle style;    // 默认：红涨 / 青跌，阳线空心
zplot::AppendKLine    (v, bars, vp, style);
zplot::AppendBamboo   (v, bars, vp, style);
zplot::AppendCloseLine(v, bars, vp, style);
zplot::AppendTower    (v, bars, vp, style);
```

```
  AppendKLine      3606 verts (1202 tris)
  AppendBamboo     3600 verts (1200 tris)
  AppendCloseLine  1206 verts (402 tris)
  AppendTower      1200 verts (400 tris)
```

默认调色板遵循国内习惯：**红涨、青跌**。要换成欧美习惯：

```cpp
zplot::KLineStyle western;
western.up      = zplot::Color4f::FromHex(0x1D9E75);   // 绿涨
western.down    = zplot::Color4f::FromHex(0xE24B4A);   // 红跌
western.hollowUp = false;                              // 实心实体
```

## 八、把顶点接进你的渲染器

示例：[`examples/05_backend.cpp`](examples/05_backend.cpp)

### 顶点布局

```
sizeof(PlotVertex) = 24 bytes = 6 floats
offsets: x=0 y=4 r=8 g=12 b=16 a=20
```

就是 6 个紧密排布的 float，编译期有断言保证：

```cpp
static_assert(sizeof(PlotVertex) == 6 * sizeof(float), "...");
```

所以两个属性指针永远是：

| 属性 | 大小 | 偏移 |
|---|---|---|
| 位置（xy） | 2 floats | 0 |
| 颜色（rgba） | 4 floats | 8 字节 |

### 不需要索引缓冲

顶点以三角形为单位背靠背排列，所以一次调用就画完：

```cpp
glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(zplot::PlotVertex),
             verts.data(), GL_STATIC_DRAW);
glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
```

300 根数据、三面板布局的实测：

```
  price (KLine + MA)     drawArrays(GL_TRIANGLES, 0, 17250)   // 5750 tris
  volume (CJL)           drawArrays(GL_TRIANGLES, 0, 8832)   // 2944 tris
  oscillator (MACD)      drawArrays(GL_TRIANGLES, 0, 8256)   // 2752 tris
```

### 保持面板对齐

**这是最容易犯的错。** 每个 `SeriesPlot` 都带着**它所基于的那段数据**的横轴
范围。显示窗口不同的面板，如果不从价格面板复制横轴范围，就对不齐：

```cpp
const zplot::Viewport priceVp =
    zplot::FitViewport(bars, 0.0f, 0.0f, W, pricePanelH, 200);   // 最后 200 根

zplot::SeriesPlot vol = zplot::ComputeVolume(bars, "CJL", { 5, 10 });

zplot::Viewport volVp;
volVp.left = 0.0f;
volVp.top = pricePanelH + 8.0f;
volVp.width = W;
volVp.height = volPanelH;
volVp.xMin = priceVp.xMin;      // 不是 vol.xMin
volVp.xMax = priceVp.xMax;      // 不是 vol.xMax
volVp.yMin = vol.yMin;
volVp.yMax = vol.yMax;
```

两种写法实测的差别：

```
  price panel x=[100,299]
  MA overlay  x=[100,299]  <- copied from price
  volume      x=[100,299]  <- copied from price (not from vol)
  oscillator  x=[100,299]  <- copied from price
  all four match: yes
  what vol.xMin/vol.xMax would have been: [0,299]
```

用成交量自己的范围会得到 `[0,299]` —— 相对只显示 `[100,299]` 的价格面板，
**横向错开 100 根 K 线**。

只有 `yMin` / `yMax` 取各自 plot 的；`xMin` / `xMax` 永远从价格面板继承。

### 逐条序列单独绘制

要独立开关图层，就按序列上传：

```
  MA series 0            drawArrays(GL_TRIANGLES, 0, 3546)   // 1182 tris
  MA series 1            drawArrays(GL_TRIANGLES, 0, 3486)   // 1162 tris
  MA series 2            drawArrays(GL_TRIANGLES, 0, 3366)   // 1122 tris
  MA series 3            drawArrays(GL_TRIANGLES, 0, 3246)   // 1082 tris
```

## 九、自己验证这些示例

本文每个示例都由同一个脚本编译**并运行**：

```
docs/examples/verify-examples.ps1
```

它先编译四个库源文件，然后逐个编译 `.cpp`、运行、**只有编译干净且退出码为
0** 才算通过。预期结果：

```
=== verifying samples ===
[ OK ] 01_quickstart
[ OK ] 02_indicators
[ OK ] 03_geometry
[ OK ] 04_theme_and_range
[ OK ] 05_backend

---------------- 5 ok, 0 failed ----------------
```

脚本直接指向工具链而不调用 `vcvarsall.bat`，所以普通命令行也能跑。MSVC 和
Windows SDK 会自动探测，装在非常规位置时用环境变量覆盖：

```
set ZPLOT_MSVC=D:\path\to\VC\Tools\MSVC\14.xx.xxxxx
set ZPLOT_KITS=D:\Windows Kits\10
```

## 十、常见错误

| 现象 | 原因 | 处理 |
|---|---|---|
| 画不出图，也没有报错 | 参数个数少于下限 | 见第五节；判 `valid` |
| `ASl` 画不出图 | 传了参数 | 传 `{}` |
| 浅色界面上线条全看不到 | 默认调色板按深色调 | `DimForLightBackground()` |
| 多个面板横向错位 | 各面板用了自己的 `xMin`/`xMax` | 从价格面板复制横轴范围 |
| 图形整体偏移 | `Bar::index` 起始值不是 0 | 从 0 重新编号 |
| 图上有断层 | `Bar::index` 不连续 | 补齐或重新编号 |
| 着色器里读错 `PlotVertex` | 误以为有对齐填充 | 就是 6 个紧密 float |
| 加了新叠加层后旧的消失了 | 追加之前先清空了 vector | 追加之间不要 clear |
| 出现 NaN 顶点 | 手工配的 `Viewport` 里 `yMin == yMax` | 用 `FitViewport` 或 `PadRange` |

---

# 定制开发服务

zplot 的开源部分永远免费，issue 和 PR 永远欢迎。

如果你需要的是**在 zplot 之上**做的事，而不是改它本身，我接定制开发：

- **指标移植** —— Pine Script、通达信或文华公式，转成 C++ / MT5
- **K 线控件集成** —— 把图表接进你现有的软件或交易终端
- **CTP 行情与交易对接** —— 基于 CTP API 的一层 C++ 封装，
  **不随本仓库发布**（详见下方）
- **指标口径对数** —— 让你的实现与参考软件逐根对齐

我做的是图表软件的**开发**。**不接**代客操盘、收益分成、信号推荐，
也不提供任何形式的投资建议。这不只是平台风控，是法律红线。

## CTP 交易接口封装（国内）

除开源的图表 / 指标外，另有一层对上期技术 CTP 交易 / 行情接口的 C++ 封装：
把 `CThostFtdcTraderApi` / `CThostFtdcMdApi` 的裸指针、回调与 GBK 字符串收进
一个值语义的接口（登录、结算单、报单、撤单、成交、持仓、行情订阅，自动
GBK → UTF-8）。**这层封装不随仓库发布**，以二进制形式提供。

- 基于 **CTP API v6.7.11**（x64，se 流，`20250617_traderapi64_se_windows`）
- 交付物：一个公开头 `ZeCtp.h`（零 CTP 依赖）+ 按 CRT 组合编好的二进制库
- CTP 官方分发包（`thosttraderapi_se` / `thostmduserapi_se` 的 `.lib` 与
  `.dll`）需自行向期货公司 / 上期技术获取，**不代发**

CTP 是国内的期货接口，海外用不上，所以这一节只面向国内用户。

## 联系方式

- **国内 / 海外** —— 闲鱼、Fiverr、知乎的完整联系方式与链接见
  [CONTACT.md](../CONTACT.md)
- **库本身的问题** —— 到 [Z-ze489/zplot](https://github.com/Z-ze489/zplot)
  开 issue
