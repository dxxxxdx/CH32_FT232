# CH32-FT232

这是一个用 CH32 模拟 FT232/FT2232、给 FPGA 当下载器用的小项目。

项目的目标很朴素：两块钱能解决的烧录器，就直接板载或者随手丢一个，没必要整得太贵。

## 你需要知道的

1. 推荐型号是 **CH32V203G6U6**，目前不需要外部晶振。同为 CH32V203 的其他型号应该也能跑，不过 Flash、RAM、中断表和外设可能有区别，换型号时自己核对一下。

2. USB 用的是 CH32 自带的 USBD 物理层。换成其他 USB 外设当然也能移植，但端点缓冲区、枚举、短包和空包这些东西得重新适配，挺折磨人的。

3. USB BSP 这一层耦合比较重，我也不喜欢一大坨代码，不过 USB 枚举本来就是个烦人的状态机，强行拆开不一定更好看。

4. 当前 JTAG 引脚是：

   - TCK：PA4
   - TDI：PA5
   - TDO：PA6
   - TMS：PA7

   TCK、TDI、TMS 尽量放在同一个 GPIO 端口。TDO 也别悬空，没接目标时至少保证它有个稳定的空闲高电平。

5. USB 串口固定为 **115200 8N1**，RX/TX 都走 DMA，缓冲区各 512 字节。UART 可以在编译时二选一：

   - `PA23`：PA2/PA3，默认；
   - `PB67`：PB6/PB7。

   ```bash
   cmake -S . -B build -DUART_FORWARD_PORT_SELECT=PA23
   cmake -S . -B build-pb67 -DUART_FORWARD_PORT_SELECT=PB67
   ```

6. PB0 接了一个开漏运行灯，低电平点亮。固件正常跑起来以后它会闪。

7. 已经在 Gowin GW1NZ-1 上试过，Windows 版 Gowin Programmer 可以直接烧 SRAM 和片内 Flash，`openFPGALoader` 也能正常识别 JTAG 链：

   ```bash
   openFPGALoader -c ft2232 --detect
   ```

8. Linux 版 Gowin Programmer 的兼容性比较玄学，尤其是新发行版。仓库里留了一个 [`gowin-ubuntu26-startprogrammer.sh`](gowin-ubuntu26-startprogrammer.sh)，需要时可以拿去看日志或者绕一下它的 USB 问题。

9. JTAG/MPSSE 的 USB 接收队列为 **4 KiB**，发送队列为 **512 B**，均静态分配。接收侧先缓存一批，再关中断连续执行；短包、队列不足容纳下一包或末包后空闲 2 ms 时开始执行。这个机制参考了 BL702，避免每个 64 字节 USB 包都打断 Flash 编程。USB 端点包长仍为 64 字节，UART 缓冲区另计；剩余 RAM 以链接器输出为准。

10. 如果 bitstream 很大或者经常烧 Flash，速度就别抱太高期待了。目前没有做 SPI 直连加速，主打一个能用。

11. JTAG/MPSSE 部分大量参考了 Sipeed 的 BL702 模拟 FT232 方案。需要 UART 屏显或者 FPGA 串口扩展的话，也可以看看 [GowinFPGA_UARTExtendBoard](https://github.com/dxxxxdx/GowinFPGA_UARTExtendBoard)。

12. 这个项目有不少代码是 Agent 写的，我负责需求、架构和上板验证。别误会，不是作者只会写成这样，主要是不想把人生耗在一个两块钱烧录器上。

13. 示波器实测2mhz的tck速度，真的一点不慢了，牛逼克拉斯144mhz主频爆杀stm32可不是闹着玩的

14. ai写了个led服务，没特别测试过，想点灯自己接钩子就好了
> **最后提醒：**目前 JTAG 频率调节还不支持，上位机设置的速度会被直接忽略；MPSSE 指令也没有全部实现，只覆盖了现阶段实际用到的下载流程。换软件、换芯片或者玩冷门命令之前，先默认这里欠支持。

## 缓冲与批处理回归

```bash
python3 -B tests/host/test_rx_batch.py
```

该测试在主机上编译真实 service、MPSSE 解析器和环形队列，覆盖批次连续执行、
队列回绕、64 字节整数倍传输的超时释放、TX 背压以及 reset/purge。
使用 `-O0/-O2` 和 UBSan，不访问下载器；真实 GPIO 时序仍以上板验证为准。
