# 生成构建时 FTDI USB 序列号后缀头
# =============================================================================
# 被 CMakeLists.txt 的 ftdi_usb_serial_header 目标以 -P 脚本模式调用：
#
#   cmake -DOUTPUT_FILE=<...>/generated/ftdiUsbBuildSerial.h \
#         -P cmake/generateFtdiUsbSerial.cmake
#
# 产出（供 usb_bsp/ft232Descriptor.c 使用）：
#
#   #define FTDI_USB_BUILD_SERIAL_SUFFIX_LENGTH 12
#   #define FTDI_USB_BUILD_SERIAL_SUFFIX_UTF16 \
#       0x32U, 0x00U, 0x36U, 0x00U, ... （共 24 字节）
#
# 契约（与 ft232Descriptor.h / ft232Descriptor.c 的静态断言绑定）：
#   - 描述符正文是 'CH32_FTDI_' 的 UTF-16LE（10 字符 = 20 字节）；
#   - 加上 12 字符 = 24 字节的后缀，再加 2 字节 bLength/bDescriptorType，
#     合计 FTDI_USB_SERIAL_DESC_SIZE = 46；
#   - ft232Descriptor.c 末尾的 _Static_assert 会用 SUFFIX_LENGTH 复核这个等式。
#   所以后缀**必须正好 12 字符**，两个宏必须同时发布、且长度一致。
#
# 设计取舍：
#   1. 用 UTC，不用本地时区——构建机换时区不该改变序列号。
#   2. 每次构建无条件重写文件，不做增量比较：序列号就是"这次构建"的标识，
#      简单可预测比省一次写入重要。
#   3. 只有秒级分辨率（12 位放不下纳秒）。同一秒内连续构建两次会得到相同
#      序列号，这是刻意接受的限制；换固件时多等一秒即可。
#   4. 格式串里每一项都用固定宽度（%y/%m/%d/%H/%M/%S 都是 2 位），
#      长度在下面被断言为 12；换格式串会在这里先报错，而不是等到链接期。
# =============================================================================

if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "generateFtdiUsbSerial.cmake: 必须传入 -DOUTPUT_FILE=<path>")
endif()

# 秒级 UTC 时间戳，必须正好 12 个字符。
#
# 不用 %Y%m%d%H%M%S：那是 14 位。这里先取 epoch 秒（有符号 64 位，允许未来），
# 再用它把目标格式转成时间字符串。
string(TIMESTAMP BUILD_EPOCH "%s" UTC)
set(SERIAL_CHARS 12)
set(SERIAL_FORMAT "%y%m%d%H%M%S")   # 2+2+2+2+2+2 = 12
string(TIMESTAMP BUILD_STAMP "${SERIAL_FORMAT}" UTC)
if(NOT BUILD_STAMP)
    message(FATAL_ERROR
        "generateFtdiUsbSerial.cmake: 无法由 epoch ${BUILD_EPOCH} 生成时间戳")
endif()
string(LENGTH "${BUILD_STAMP}" STAMP_LENGTH)
if(NOT STAMP_LENGTH EQUAL SERIAL_CHARS)
    message(FATAL_ERROR
        "generateFtdiUsbSerial.cmake: 格式串 '${SERIAL_FORMAT}' 产生的时间戳 "
        "'${BUILD_STAMP}' 长度为 ${STAMP_LENGTH}，预期 ${SERIAL_CHARS}；"
        "长度变化会破坏 FTDI_USB_SERIAL_DESC_SIZE 契约")
endif()

# 数字字符 -> UTF-16LE 字节对，直接查表。时间戳只可能是数字，查不到即为异常。
set(DIGIT_UTF16
    "0x30U, 0x00U" "0x31U, 0x00U" "0x32U, 0x00U" "0x33U, 0x00U" "0x34U, 0x00U"
    "0x35U, 0x00U" "0x36U, 0x00U" "0x37U, 0x00U" "0x38U, 0x00U" "0x39U, 0x00U")

set(UTF16_BYTES "")
set(INDEX 0)
while(INDEX LESS SERIAL_CHARS)
    string(SUBSTRING "${BUILD_STAMP}" ${INDEX} 1 CHAR)
    if(NOT CHAR MATCHES "^[0-9]$")
        message(FATAL_ERROR
            "generateFtdiUsbSerial.cmake: 时间戳里出现非数字字符 '${CHAR}'")
    endif()
    list(GET DIGIT_UTF16 ${CHAR} BYTE_PAIR)
    # 每一行都要自带续行反斜杠。少一个反斜杠，宏就会提前结束，后面几行会
    # 变成裸的十六进制常量，编译器报 "expected identifier or '('"。
    # 这里用 \\\\n 让 file(WRITE) 得到"反斜杠 + 换行"。
    string(APPEND UTF16_BYTES "    ${BYTE_PAIR},\\\n")
    math(EXPR INDEX "${INDEX} + 1")
endwhile()

file(WRITE "${OUTPUT_FILE}"
"/*
 * 本文件由 cmake/generateFtdiUsbSerial.cmake 自动生成，每次构建都会重写。
 * 不要手工编辑，也不要提交进仓库。
 *
 * 构建时间（UTC）：${BUILD_STAMP}
 * 用途：拼接 FtdiUsbSerialDescriptor 里 'CH32_FTDI_' 之后的 ${SERIAL_CHARS} 字符后缀。
 */
#ifndef CH32_FT232_FTDI_USB_BUILD_SERIAL_H
#define CH32_FT232_FTDI_USB_BUILD_SERIAL_H

/* 后缀字符数。ft232Descriptor.c 的静态断言用它复核描述符总长。 */
#define FTDI_USB_BUILD_SERIAL_SUFFIX_LENGTH ${SERIAL_CHARS}
#define FTDI_USB_BUILD_SERIAL_ASCII \"CH32_FTDI_${BUILD_STAMP}\"

/* ${SERIAL_CHARS} 个 UTF-16LE 字符 = ${SERIAL_CHARS} * 2 字节，正好补足 FTDI_USB_SERIAL_DESC_SIZE(46)。 */
#define FTDI_USB_BUILD_SERIAL_SUFFIX_UTF16 \\
${UTF16_BYTES}
#endif /* CH32_FT232_FTDI_USB_BUILD_SERIAL_H */
")

message(STATUS "FTDI USB 序列号后缀（UTC）：${BUILD_STAMP}")
