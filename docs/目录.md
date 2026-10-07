# zplot 文档目录

zplot 是一个 **C++20 的 K 线 / 技术指标库**：把 OHLC 数据算成**三角形顶点**，
自己不画图 —— 输出几何，交给任意渲染器。

零第三方依赖，不碰任何图形 API。当前 **59 个指标**，与国内交易软件的
同名公式逐项对齐。

## 从这里开始

| 文档 | 内容 |
|---|---|
| [开发者使用指南](development.zh.md) | **推荐先读**。从编译到接上渲染器的完整链路，每段示例都已编译运行 |
| [Developer guide](development.md) | 同上，英文版 |
| [指标参考](indicators.zh.md) | 59 个指标的名字、公式、默认参数 |
| [Indicator reference](indicators.md) | 同上，英文版，附与文华 wh6 / 通达信的对账结果 |

## 按主题查

| 主题 | 去哪看 |
|---|---|
| 编译、CMake 接入 | [开发者指南 · 二](development.zh.md#二编译) |
| 数据格式 `Bar` 的硬规则 | [开发者指南 · 三](development.zh.md#三数据契约) |
| 整体用法（先算再画） | [开发者指南 · 四](development.zh.md#四两个阶段先算再画) |
| 指标怎么调、参数给几个 | [开发者指南 · 五](development.zh.md#五指标) |
| 自己画水平线 / 标记 | [开发者指南 · 六](development.zh.md#六几何) |
| 颜色、浅色背景、自适应范围 | [开发者指南 · 七](development.zh.md#七颜色范围与自适应) |
| 接 OpenGL / D3D / Vulkan | [开发者指南 · 八](development.zh.md#八把顶点接进你的渲染器) |
| 面板横向错位 | [开发者指南 · 八](development.zh.md#保持面板对齐) |
| 排错速查表 | [开发者指南 · 十](development.zh.md#十常见错误) |

## 示例代码

全部在 [`examples/`](examples/) 下，每个都能独立编译运行：

| 文件 | 演示什么 |
|---|---|
| [`01_quickstart.cpp`](examples/01_quickstart.cpp) | 最小完整流程：算指标 → 配视口 → 出顶点 |
| [`02_indicators.cpp`](examples/02_indicators.cpp) | 59 个指标全跑一遍，含参数个数陷阱 |
| [`03_geometry.cpp`](examples/03_geometry.cpp) | 全部几何图元与折线断点 |
| [`04_theme_and_range.cpp`](examples/04_theme_and_range.cpp) | 浅色背景陷阱、取值范围、4 种 K 线样式 |
| [`05_backend.cpp`](examples/05_backend.cpp) | 顶点布局、三面板对齐、逐层上传 |
| [`verify-examples.ps1`](examples/verify-examples.ps1) | 一键编译并运行上面全部示例 |

```
powershell -ExecutionPolicy Bypass -File docs\examples\verify-examples.ps1
```

## 什么是"零依赖"（准确说法）

核心库**零第三方依赖、零图形 API 依赖** —— 只用 C++ 标准库。

它**不包含渲染引擎**：拿到顶点之后接哪个后端由你决定。仓库里的 demo 用
Win32 + OpenGL 4.6，那是 demo 的选择，不是库的约束。

**解耦不等于没有依赖** —— demo 跑起来当然需要 OpenGL 驱动。

---

# 定制开发服务

zplot 的开源部分永远免费。如果你需要的是**在它之上**做的事，我接定制：

- **指标移植** —— Pine Script / 通达信 / 文华 → C++ / MT5
- **K 线控件集成** —— 把图表接进你现有的软件或交易终端
- **指标口径对数** —— 让你的实现与参考软件逐根对齐
- **CTP 行情与交易对接** —— 国内期货接口，**仅闲鱼 / 知乎渠道**

**不接**：代客操盘、收益分成、信号推荐，以及任何形式的投资建议。

联系：闲鱼、Fiverr、知乎的完整联系方式见 [CONTACT.md](../CONTACT.md)。
