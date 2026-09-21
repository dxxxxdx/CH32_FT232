# LOCAL_IDLE实测失败与连续提交候选版

> 历史调试记录：正文中的状态、路径与候选版名称均指记录当时。当前用法见[项目 README](../readme.md)，最终 Flash 修复结果见[成功记录](gw1n9-flash-success-20260921.md)。本地日志、工具和旧固件路径不作为发布附件。

后续20:48：本文ATOMIC_COMMIT候选版单页上板仍失败；最新DR_TRANSACTION候选版暂存进入Shift-DR命令，见[后续记录](gw1n9-atomic-result-20260921.md)。

2026-09-21 20:37，用户确认烧入后，USB核对为`CH32_FTDI_260921122636`，符合上一版LOCAL_IDLE固件。只有该CH32下载器，没有并行运行的Programmer/minicom。FPGA原始ID由Python确认1100481B；OFL显示的100481B是其型号识别显示。

## 本轮硬件结果

日志目录：`logs/gw1n9-normal-local-idle-20260921-203708/`。

1. 使用`/usr/bin/openFPGALoader v0.13.1`，同一`/home/dxxdx/FPGAProjects/9K_LED_project.fs`，完整Flash烧录一次。文件SHA256为`7f7da59b6dabd011c751d7f26db1ebd47de11adbe5b47f1c877e99103d4ba06b`。最终CRC FAIL，USERCODE=0而期望63BD，状态39020。退出码仍为0，不能算成功。见`ofl-flash.log`、`program-exit.json`。
2. 运行一次`normal_direct_style.py`：沿用成功直驱版的IR两拍退出加六拍Idle、DR两拍退出，擦除的enable/75/地址前缀48字节，连同2字节执行屏障在一个64字节USB包内；不再传长零流等待，由固件提供600000拍。屏障耗时`0.3380009919983422`秒，擦除reload后仍39020。
3. 同一次脚本尝试写入与直驱相同的64字图样，页体和屏障1232字节，页头命中现有批次收集策略；写页reload后仍39020。Flash Lock门控不通过，没有把无效返回当成Flash数据，`words_read=0`、`mismatches=null`。见`direct-style/result.json`及原始`trace.jsonl`。

本轮没有自动重试，没有刷写MCU，也没有写外置Flash。当前FPGA未恢复LED设计，正常烧录故障仍在；不能将前一轮直驱的单页成功说成正常烧录已修复。

## 剩余差异与本次修改

直驱成功路径是DR32函数返回后，直接调用两拍Update/Idle，再调用连续等待。LOCAL_IDLE版虽已将等待接到退出命令后，DR32尾拍先执行，随后还会完成当前命令、从RX取下一条命令、解析参数、更新状态，再真正输出Update/Idle；这个间隙没有被上一版消除。

ATOMIC_COMMIT候选版将原4字节暂存扩展为记录末拍的1字节字段，使用已有阶段值5表示“DR32和末拍已齐，返回后缀尚待确认”。只接受两拍/七拍返回Idle，允许中间无边沿87；非法后缀停止并等reset。返回命令收齐后才输出DR32、两拍返回、连续等待，中间不再取RX或运行解析器。所有状态仍由Gowin内核维护，USB不维护第二份TAP或Flash事务。

复用原字段后JtagState仍28字节，RX4096/TX1024不变；准备1200、擦除600000、地址32、字后24拍均不变。普通JTAG读回及非融合数据路径不改。交付关闭ACM及UART。该改动针对明确存在的软件间隙，但物理波形未测量，尚不能认定它就是故障根因。

## 交付与检查

- 目录：`firmware-22pinout-atomic-commit-20260921-2043/`。
- BIN：`CH32_FT232_22PINOUT_ATOMIC_COMMIT.bin`。
- 串号：`CH32_FTDI_260921124313`，固定22pinout。
- FLASH/bin15920字节；RAM7668字节，含2048字节预留栈。
- 编译链接通过，没有编译警告；`git diff --check`通过。按用户要求未跑本地测试套件。
- 底层DR32及Run-Test机器码与成功直驱版逐字节一致。反汇编中的`jtag_commit_dr32`为70字节，连续调用DR32、TMS两拍及等待，没有解析器调用；GPIO函数自身的序言/尾声仍存在。
- 独立构建目录`build-22-atomic-commit`，`COPY_ELF_TO_SOURCE=OFF`，根目录既有ELF保持原SHA256。

剩余限制：等待数据期间物理TAP仍在Shift-DR但尚未输出该字；准备结束到IR15、进入Shift-DR到首拍、字与字之间仍有解析开销。此次不改动这些位置，便于用同一单页实验判断提交间隙的影响。用户手动刷入后，先核对新串号并重复单页诊断，成功后再跑完整FS及掉电启动验证。此次尚未对新候选版执行板上测试。
