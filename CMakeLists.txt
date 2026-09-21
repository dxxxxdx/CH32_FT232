cmake_minimum_required(VERSION 3.15)

# =========================================================================
# 1. Toolchain, must be before project()
# =========================================================================
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(TOOLCHAIN_PATH "/home/dxxdx/pub/RISC-V Embedded GCC15")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-gcc" CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-g++" CACHE FILEPATH "CXX compiler" FORCE)
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-gcc" CACHE FILEPATH "ASM compiler" FORCE)

set(CMAKE_OBJCOPY "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-objcopy" CACHE FILEPATH "objcopy" FORCE)
set(CMAKE_OBJDUMP "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-objdump" CACHE FILEPATH "objdump" FORCE)
set(CMAKE_SIZE    "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-size" CACHE FILEPATH "size" FORCE)

set(CMAKE_AR     "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-gcc-ar" CACHE FILEPATH "ar" FORCE)
set(CMAKE_RANLIB "${TOOLCHAIN_PATH}/bin/riscv32-wch-elf-gcc-ranlib" CACHE FILEPATH "ranlib" FORCE)

if(NOT EXISTS "${CMAKE_C_COMPILER}")
    message(FATAL_ERROR "C compiler not found: ${CMAKE_C_COMPILER}")
endif()

if(NOT EXISTS "${CMAKE_OBJCOPY}")
    message(FATAL_ERROR "objcopy not found: ${CMAKE_OBJCOPY}")
endif()

# =========================================================================
# 2. Project
# =========================================================================
project(CH32_FT232 C CXX ASM)

message(STATUS "WCH CC      = ${CMAKE_C_COMPILER}")
message(STATUS "WCH CXX     = ${CMAKE_CXX_COMPILER}")
message(STATUS "WCH ASM     = ${CMAKE_ASM_COMPILER}")
message(STATUS "WCH OBJCOPY = ${CMAKE_OBJCOPY}")
message(STATUS "WCH OBJDUMP = ${CMAKE_OBJDUMP}")
message(STATUS "WCH SIZE    = ${CMAKE_SIZE}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# =========================================================================
# 3. Target / linker script
# =========================================================================
set(TARGET_NAME CH32_FT232)
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld")
set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FTDI_USB_SERIAL_HEADER "${GENERATED_INCLUDE_DIR}/ftdiUsbBuildSerial.h")

# 硬件需求只读取 GUI 保存的文件，旧板型/UART 缓存不能覆盖它。
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/loadUserConfig.cmake")
set(PINSELECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/userconfig.cmake")
pinselect_load_config("${PINSELECT_CONFIG_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/boardtype")
set(BOARD_DEFINITION "${PINSELECT_BOARD_DEFINE}")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/boardtype/UserBoardConfig.h.in"
        "${GENERATED_INCLUDE_DIR}/userBoardConfig.h" @ONLY)
if(DEFINED BOARD_TYPE OR DEFINED UART_FORWARD_PORT_SELECT OR DEFINED UART_FORWARD_ENABLE)
    message(STATUS "Legacy board/UART options are ignored; hardware comes from userconfig.cmake")
endif()

option(RUN_LED_ENABLE "Enable the selected board's run LED" OFF)
option(JTAG_ACM_TRACE "Use CDC ACM exclusively for deferred JTAG diagnostics" OFF)
set(UART_FORWARD_BUILD_ENABLED OFF)
if(PINSELECT_HAS_UART)
    set(UART_FORWARD_BUILD_ENABLED ON)
endif()
if(JTAG_ACM_TRACE)
    set(UART_FORWARD_BUILD_ENABLED OFF)
    message(STATUS "JTAG ACM trace = ON; CDC reserved for diagnostics")
endif()
if(RUN_LED_ENABLE AND NOT PINSELECT_MODE STREQUAL "20PINOUT")
    message(FATAL_ERROR "The selected configuration has no run LED; set RUN_LED_ENABLE=OFF")
endif()
message(STATUS "Final config file = ${PINSELECT_CONFIG_FILE}")
message(STATUS "Board preset = ${PINSELECT_MODE}")
message(STATUS "JTAG: TCK=${PINSELECT_TCK}, TMS=${PINSELECT_TMS}, TDI=${PINSELECT_TDI}, TDO=${PINSELECT_TDO}")
message(STATUS "UART route = ${PINSELECT_UART_DESCRIPTION}; forwarding=${UART_FORWARD_BUILD_ENABLED}; 115200 8N1")
message(STATUS "USB: DM=PA11, DP=PA12")
message(STATUS "Run LED = ${RUN_LED_ENABLE}; JTAG ACM trace = ${JTAG_ACM_TRACE}")
message(STATUS "FTDI USB serial timestamp is generated independently by CMake at build time (UTC)")

if(NOT EXISTS "${LINKER_SCRIPT}")
    message(FATAL_ERROR "Linker script not found: ${LINKER_SCRIPT}")
endif()

# =========================================================================
# 4. Flags from native MounRiver Makefile
# =========================================================================
set(ARCH_FLAGS
        -march=rv32imacxw
        -mabi=ilp32
        -msmall-data-limit=8
)

set(COMMON_COMPILE_FLAGS
        ${ARCH_FLAGS}
        -msave-restore
        -fmax-errors=20
        -Og
        -fmessage-length=0
        -fsigned-char
        -ffunction-sections
        -fdata-sections
        -fno-common
        -Wunused
        -Wuninitialized
        -g3
)

set(COMMON_LINK_FLAGS
        ${ARCH_FLAGS}
        -msave-restore
        -fmax-errors=20
        -Og
        -fmessage-length=0
        -fsigned-char
        -ffunction-sections
        -fdata-sections
        -fno-common
        -Wunused
        -Wuninitialized
        -g3
        "-T${LINKER_SCRIPT}"
        -nostartfiles
        -Wl,--gc-sections
        "-Wl,-Map,${TARGET_NAME}.map"
        --specs=nano.specs
        --specs=nosys.specs
        -Wl,--print-memory-usage
)

# =========================================================================
# 5. Source files
# =========================================================================
file(GLOB_RECURSE PROJECT_C_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

file(GLOB_RECURSE PROJECT_CXX_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

file(GLOB_RECURSE PROJECT_ASM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.s"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.S"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.asm"
)

# Exclude CMake / build generated files
foreach(_source_list PROJECT_C_SOURCES PROJECT_CXX_SOURCES PROJECT_ASM_SOURCES)
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/cmake-build[^/]*/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/build[^/]*/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/CMakeFiles/.*")
    # 主机回归包含模拟寄存器和独立 main，不能混入 MCU 固件。
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/tests/.*")
    # 日志中的源码快照与独立直驱诊断有自己的入口，不能链接到正常固件。
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/logs/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/firmware-[^/]*/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/host_tools/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/diagnostics/gw1n9_direct/.*")
endforeach()

if(NOT UART_FORWARD_BUILD_ENABLED)
    list(FILTER PROJECT_C_SOURCES EXCLUDE REGEX ".*/uart_forward/.*\\.c$")
endif()
if(NOT UART_FORWARD_BUILD_ENABLED AND NOT JTAG_ACM_TRACE)
    list(FILTER PROJECT_C_SOURCES EXCLUDE REGEX ".*/usb_bsp/ft232CdcPort\\.c$")
endif()

set_source_files_properties(${PROJECT_ASM_SOURCES} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET_NAME}
        ${PROJECT_C_SOURCES}
        ${PROJECT_CXX_SOURCES}
        ${PROJECT_ASM_SOURCES}
        boardtype/BoardConfig.h
)

# 该 target 没有 OUTPUT，因此每次构建都会重新生成秒级 UTC 序列号。
# descriptor object 显式依赖生成头，内容变化后必须重编并重新链接。
add_custom_target(ftdi_usb_serial_header ALL
        COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT_FILE=${FTDI_USB_SERIAL_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generateFtdiUsbSerial.cmake"
        BYPRODUCTS "${FTDI_USB_SERIAL_HEADER}"
        COMMENT "Generating build-time FTDI USB serial number..."
        VERBATIM
)
add_dependencies(${TARGET_NAME} ftdi_usb_serial_header)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/usb_bsp/ft232Descriptor.c"
        APPEND PROPERTY OBJECT_DEPENDS "${FTDI_USB_SERIAL_HEADER}")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/diagnostics/jtagTrace.c"
        APPEND PROPERTY OBJECT_DEPENDS "${FTDI_USB_SERIAL_HEADER}")

set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "${TARGET_NAME}"
        SUFFIX ".elf"
)

# =========================================================================
# 6. Include directories
# =========================================================================
target_include_directories(${TARGET_NAME} PRIVATE
        "${GENERATED_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Startup"
        "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/inc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Peripheral/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/Debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core"
        "${CMAKE_CURRENT_SOURCE_DIR}/User"
)

# Auto include all dirs containing .h files
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/cmake-build[^/]*/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/build[^/]*/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/CMakeFiles/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/tests/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/logs/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/firmware-[^/]*/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/host_tools/.*")
list(FILTER PROJECT_HEADERS EXCLUDE REGEX ".*/diagnostics/gw1n9_direct/.*")

set(PROJECT_HEADER_DIRS "")
foreach(_header ${PROJECT_HEADERS})
    get_filename_component(_dir "${_header}" DIRECTORY)
    list(APPEND PROJECT_HEADER_DIRS "${_dir}")
endforeach()

list(REMOVE_DUPLICATES PROJECT_HEADER_DIRS)

target_include_directories(${TARGET_NAME} PRIVATE
        ${PROJECT_HEADER_DIRS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
        CH32V20x_D6
        ${BOARD_DEFINITION}
        UART_FORWARD_ENABLED=$<BOOL:${UART_FORWARD_BUILD_ENABLED}>
        RUN_LED_ENABLED=$<BOOL:${RUN_LED_ENABLE}>
        JTAG_ACM_TRACE_ENABLED=$<BOOL:${JTAG_ACM_TRACE}>
)

# =========================================================================
# 7. Compile and link options
# =========================================================================
target_compile_options(${TARGET_NAME} PRIVATE
        ${COMMON_COMPILE_FLAGS}
        $<$<COMPILE_LANGUAGE:ASM>:-x>
        $<$<COMPILE_LANGUAGE:ASM>:assembler-with-cpp>
)

target_link_options(${TARGET_NAME} PRIVATE
        ${COMMON_LINK_FLAGS}
)

# =========================================================================
# 8. Static libraries
# =========================================================================
file(GLOB_RECURSE ALL_LIB_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.a"
)

if(ALL_LIB_FILES)
    list(FILTER ALL_LIB_FILES EXCLUDE REGEX ".*/cmake-build[^/]*/.*")
    list(FILTER ALL_LIB_FILES EXCLUDE REGEX ".*/build[^/]*/.*")
    list(FILTER ALL_LIB_FILES EXCLUDE REGEX ".*/CMakeFiles/.*")

    target_link_libraries(${TARGET_NAME} PRIVATE
            ${ALL_LIB_FILES}
    )
endif()

# =========================================================================
# 9. Post-build outputs: HEX / LST / ASM / ELF copy
# =========================================================================
# 默认保留IDE工作流；诊断构建显式关闭，不能覆盖用户当前保留的根目录ELF。
option(COPY_ELF_TO_SOURCE "Copy built ELF back to the source directory" ON)
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}"
        -O ihex
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${TARGET_NAME}.hex"

        COMMAND /bin/sh -c
        "\"${CMAKE_OBJDUMP}\" --source --all-headers --demangle -M xw --line-numbers --wide \"$<TARGET_FILE:${TARGET_NAME}>\" > \"${TARGET_NAME}.lst\""

        COMMAND /bin/sh -c
        "\"${CMAKE_OBJDUMP}\" -S -d -M xw \"$<TARGET_FILE:${TARGET_NAME}>\" > \"${TARGET_NAME}.asm\""

        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating HEX/LST/ASM files and displaying memory usage..."
)

if(COPY_ELF_TO_SOURCE)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy
            "$<TARGET_FILE:${TARGET_NAME}>"
            "${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.elf"
            COMMENT "Copying ELF to source directory..."
            VERBATIM
    )
endif()

set(EANALY_EXE "/bin/eanaly")
if(EXISTS "${EANALY_EXE}")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${EANALY_EXE}"
            "$<TARGET_FILE:${TARGET_NAME}>"

            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Running WCH post-analysis..."
    )
endif()

# 保持 CH32_FT232 为 CLion 的直接构建目标，所有产物和分析完成后再打印。
# 使用本次配置解析后的值，确保摘要与已编译宏一致，不在构建末尾重读文件。
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E echo ""
        COMMAND "${CMAKE_COMMAND}" -E echo "========== 最终固件配置 =========="
        COMMAND "${CMAKE_COMMAND}" -E echo "板型: ${PINSELECT_MODE}"
        COMMAND "${CMAKE_COMMAND}" -E echo
            "JTAG: TCK=${PINSELECT_TCK}  TMS=${PINSELECT_TMS}  TDI=${PINSELECT_TDI}  TDO=${PINSELECT_TDO}"
        COMMAND "${CMAKE_COMMAND}" -E echo
            "UART: ${PINSELECT_UART_DESCRIPTION} | 转发=${UART_FORWARD_BUILD_ENABLED} | 115200 8N1"
        COMMAND "${CMAKE_COMMAND}" -E echo "USB: DM=PA11  DP=PA12"
        COMMAND "${CMAKE_COMMAND}" -E echo
            "运行灯=${RUN_LED_ENABLE}  ACM调试=${JTAG_ACM_TRACE}"
        COMMAND "${CMAKE_COMMAND}" -E echo "配置文件: ${PINSELECT_CONFIG_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E echo "固件: $<TARGET_FILE:${TARGET_NAME}>"
        COMMAND "${CMAKE_COMMAND}" -E echo "=================================="
        COMMENT "Printing final firmware pin configuration..."
        VERBATIM
)
