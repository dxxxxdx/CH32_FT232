# 板级配置

硬件差异统一由 `boardtype` 定义。应用和驱动包含 `boardtype/BoardConfig.h`，GPIO 初始化接口使用 `boardtype/BoardGpio.h`。原 GPIO/UART/LED 配置头和默认引脚兜底已移除。

## 配置入口

正常固件只从项目根目录的 `userconfig.cmake` 取得硬件需求。运行 `python3 PinSelectGUI/pinselect.py`，选择预设或自定义后保存；首次打开默认为 20PINOUT，已有文件则恢复保存值。没有配置文件时 CMake 报错，不会偷偷使用某个板型。

```text
PinSelectGUI → userconfig.cmake → CMake 校验并打印
  ├─ 20PINOUT / 22PINOUT → 现有板型头
  └─ CUSTOM → 构建目录/generated/userBoardConfig.h
                     ↓
             boardtype/BoardConfig.h
                     ↓
             GPIO / UART / USB / LED
```

| 文件 | 职责 |
| --- | --- |
| `BoardConfig.h` | 唯一板型选择、必需定义及引脚范围检查 |
| `20pinout.h` / `22pinout.h` | 完整硬件预设，也是 GUI 显示预设的来源 |
| `UserBoard.h` / `UserBoardConfig.h.in` | 自定义板型入口及生成头模板 |
| `Ch32V203Usart1Pb67.h` | USART1、PB6/PB7、DMA1 通道 5/4、RX IRQ |
| `Ch32V203Usart2Pa23.h` | USART2、PA2/PA3、DMA1 通道 6/7、RX IRQ |
| `BoardFeatures.h` | 功能约束及共用的线编码、缓冲常量 |
| `BoardGpio.c/.h` | 静态 GPIO 对象及初始化、JTAG、USB 引脚执行 |

旧 `BOARD_TYPE`、`UART_FORWARD_PORT_SELECT` 和 `UART_FORWARD_ENABLE` CMake 选项不能覆盖文件选择。GPIO/DMA 驱动仍只读取统一 `BOARD_*` 宏；GUI 不直接改驱动和板型源码。

## 文件格式

20PINOUT 预设：

```cmake
set(USER_CONFIG_VERSION "1")
set(USER_BOARD_MODE "20PINOUT")
```

预设只存名称，CMake 和 GUI 直接从对应板型头取得具体引脚。预设模式中附带自定义字段会报错，避免显示的板型与实际引脚不一致。

自定义示例：

```cmake
set(USER_CONFIG_VERSION "1")
set(USER_BOARD_MODE "CUSTOM")
set(USER_JTAG_TCK "PA6")
set(USER_JTAG_TMS "PA5")
set(USER_JTAG_TDI "PA4")
set(USER_JTAG_TDO "PA0")
set(USER_UART_MODE "PA23")
```

字段格式为 `set(KEY "VALUE")`；支持空行和整行 `#` 注释。读取器仅解析这些数据，不执行配置文件里的 CMake 命令。重复键、未知键和缺失字段报错。

自定义 JTAG 只支持 PA0～PA7，四个信号必须互不重复。UART 为 `PA23`、`PB67` 或 `DISABLED`，使用 PA23 时 JTAG 不得占用 PA2/PA3。USB 固定 PA11/PA12，自定义板型没有运行灯定义。

## 构建

先按[项目构建说明](../readme.md)设置 WCH 工具链路径。在 CLion 中直接构建 `CH32_FT232` 目标，无需单独设置板型参数。编译、产物生成和 WCH 分析完成后，日志末尾会打印本次固件实际采用的引脚配置。

```sh
cmake -S . -B build-user -DCOPY_ELF_TO_SOURCE=OFF
cmake --build build-user --target CH32_FT232 -j4
```

CMake 输出最终选择；配置文件加入重新配置依赖，GUI 保存后直接构建也会重新读取。自动生成的硬件头位于构建目录，不覆盖源文件。每次构建的 FTDI USB 串号时间戳由 CMake 独立生成，编译不需要启动 GUI 或调用 Python。

`RUN_LED_ENABLE`、`JTAG_ACM_TRACE` 是功能选项，在新目录默认 OFF。前者仅能用于有运行灯的 20PINOUT，后者启用时独占 CDC 并关闭实际 UART 转发。GUI 不配置这些开关，CMake 会打印它们的最终值；已有构建目录保留其缓存设置。UART 转发和 ACM 调试均关闭时，不枚举 CDC 接口。

## 约束与扩展

板型选择宏 `BOARD_20PINOUT`、`BOARD_22PINOUT`、`BOARD_USER` 必须恰好有一个为 1。缺板型、缺必需引脚或启用不存在的资源时直接报错。

TCK/TMS/TDI 共用 `BOARD_JTAG_OUTPUT_PORT`，保证连续 DR32 输出的同端口约束。板型声明有 UART/LED 时，必须给全对应的引脚、外设、时钟、重映射及 DMA/IRQ 信息。UART 禁用的自定义配置不编译 UART 驱动。

新增固定预设时，在板型头给出硬件定义，再同步注册 CMake 读取器、GUI 预设名称和 `BoardConfig.h` 选择分支。扩展允许的自定义引脚范围时，应同时修改 GUI 和 CMake 的输入校验，并保证板级 GPIO 的约束。

UART 的 115200 8N1、512 B 收发缓冲、CDC 16 B 搬运粒度，以及 JTAG DR32 和等待时钟参数保持原值。PA23 路由沿用此前 USART2 实现的外设资源，新的自定义接线仍需上板验证。
