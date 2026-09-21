# PinSelectGUI

图形化硬件配置入口，类似 menuconfig：选择配置并保存，随后由 CMake 独立编译。GUI 不调用 CMake，也不生成固件或时间戳。

## 运行与保存

需要 Python 3.9 或更新版本及 tkinter。在项目根目录运行：

```sh
python3 PinSelectGUI/pinselect.py
```

1. 没有 `userconfig.cmake` 时默认显示 **20PINOUT**；已有有效文件时恢复保存值。
2. 选择 **20PINOUT / 22PINOUT** 使用完整预设，自定义控件保持隐藏。
3. 需要改引脚或禁用 UART 时，切换到“自定义”；初始值沿用刚才的预设。
4. 点击“保存配置”更新项目根目录的 `userconfig.cmake`。直接关闭窗口不会写文件。

| 自定义项 | 可选值 |
| --- | --- |
| JTAG TCK、TMS、TDI、TDO | PA0～PA7，四个信号必须互不重复 |
| UART | PA2/TX＋PA3/RX、PB6/TX＋PB7/RX、禁用 |

选择 PA2/PA3 UART 后，JTAG 不得再占用 PA2/PA3。冲突会显示原因并禁用保存。USB 固定为 PA11/DM、PA12/DP，不在 GUI 中配置。

预设值来自 `boardtype/20pinout.h` 和 `22pinout.h`，Python 不另存一套引脚表。预设文件只保存名称，自定义文件保存全部 JTAG 引脚和 UART 模式。文件格式见[板级配置说明](../boardtype/README.md)。

## 保存后编译

按[项目构建说明](../readme.md)设置 WCH 工具链路径。在 CLion 中直接构建 **CH32_FT232** 目标，无需另传板型或引脚参数。也可以在项目根目录执行：

```sh
cmake -S . -B build-user -DCOPY_ELF_TO_SOURCE=OFF
cmake --build build-user --target CH32_FT232 -j4
```

保存新配置后，下一次构建会自动重新读取。缺文件、字段不全或引脚冲突会终止配置。构建日志末尾显示最终板型、引脚、功能开关和 ELF 路径。

GUI 不修改 `CMakeCache.txt`。旧 `BOARD_TYPE`、`UART_FORWARD_ENABLE`、`UART_FORWARD_PORT_SELECT` 选项不能覆盖文件选择。运行灯和 ACM 调试仍由 CMake 的 `RUN_LED_ENABLE`、`JTAG_ACM_TRACE` 控制，新构建目录默认关闭；已有目录保留其缓存值。只有 20PINOUT 有运行灯定义，ACM 调试启用时占用 CDC 并关闭 UART 转发。

UART 固定为 115200 8N1。USB 串号由 CMake 在构建时生成，使用 UTC 秒级时间；保存配置不会提前生成或冻结串号。
