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

# 当前这批固件统一给 20PINOUT 板使用。FORCE 会覆盖 IDE 旧构建目录中
# 残留的 DEFAULT 缓存，恢复多板构建时再去掉 FORCE。
set(BOARD_TYPE "20PINOUT" CACHE STRING "Board type: DEFAULT or 20PINOUT" FORCE)
set_property(CACHE BOARD_TYPE PROPERTY STRINGS DEFAULT 20PINOUT)
string(TOUPPER "${BOARD_TYPE}" BOARD_TYPE_NORMALIZED)

set(BOARD_DEFINITION "")
if(BOARD_TYPE_NORMALIZED STREQUAL "DEFAULT")
    message(STATUS "Board type = DEFAULT")
elseif(BOARD_TYPE_NORMALIZED STREQUAL "20PINOUT")
    set(BOARD_DEFINITION BOARD_20PINOUT=1U)
    message(STATUS "Board type = 20PINOUT")
else()
    message(FATAL_ERROR "BOARD_TYPE must be DEFAULT or 20PINOUT, got: ${BOARD_TYPE}")
endif()

# 无状态灯的板子关闭后不会实例化 LED，也不会配置任何 LED GPIO。
option(RUN_LED_ENABLE "Enable the default PB0 run LED" ON)
if(RUN_LED_ENABLE)
    set(RUN_LED_DEFINITION RUN_LED_ENABLED=1U)
else()
    set(RUN_LED_DEFINITION RUN_LED_ENABLED=0U)
endif()

# UART 转发默认使用 20PINOUT 板的 PB6/PB7；需要纯 FT2232/JTAG 固件时
# 可显式传 DISABLED，主循环入口保持不变，由服务头提供空实现。
set(UART_FORWARD_PORT_SELECT "PB67" CACHE STRING
        "UART forwarding: DISABLED, PA23 or PB67")
set_property(CACHE UART_FORWARD_PORT_SELECT PROPERTY STRINGS DISABLED PA23 PB67)

set(UART_FORWARD_PORT_DEFINITION "")
set(UART_FORWARD_BUILD_ENABLED ON)
string(TOUPPER "${UART_FORWARD_PORT_SELECT}" UART_FORWARD_PORT_NORMALIZED)
if(UART_FORWARD_PORT_NORMALIZED STREQUAL "" OR
   UART_FORWARD_PORT_NORMALIZED STREQUAL "DISABLED")
    set(UART_FORWARD_PORT_DEFINITION
            UART_FORWARD_PORT=UART_FORWARD_PORT_DISABLED)
    set(UART_FORWARD_BUILD_ENABLED OFF)
    message(STATUS "UART forwarding = DISABLED")
elseif(UART_FORWARD_PORT_NORMALIZED STREQUAL "PA23")
    if(BOARD_TYPE_NORMALIZED STREQUAL "20PINOUT")
        message(FATAL_ERROR "20PINOUT UART forwarding only supports PB67")
    endif()
    set(UART_FORWARD_PORT_DEFINITION
            UART_FORWARD_PORT=UART_FORWARD_PORT_PA23)
    message(STATUS "UART forwarding port = PA23 (default)")
elseif(UART_FORWARD_PORT_NORMALIZED STREQUAL "PB67")
    set(UART_FORWARD_PORT_DEFINITION
            UART_FORWARD_PORT=UART_FORWARD_PORT_PB67)
    message(STATUS "UART forwarding port = PB67")
else()
    message(FATAL_ERROR
            "UART_FORWARD_PORT_SELECT must be DISABLED, PA23 or PB67, got: ${UART_FORWARD_PORT_SELECT}")
endif()

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
endforeach()

if(NOT UART_FORWARD_BUILD_ENABLED)
    list(FILTER PROJECT_C_SOURCES EXCLUDE REGEX ".*/uart_forward/.*\\.c$")
    list(FILTER PROJECT_C_SOURCES EXCLUDE REGEX ".*/usb_bsp/ft232CdcPort\\.c$")
endif()

set_source_files_properties(${PROJECT_ASM_SOURCES} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET_NAME}
        ${PROJECT_C_SOURCES}
        ${PROJECT_CXX_SOURCES}
        ${PROJECT_ASM_SOURCES}
        boardtype/20pinout.h
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
        ${UART_FORWARD_PORT_DEFINITION}
        ${RUN_LED_DEFINITION}
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
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}"
        -O ihex
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${TARGET_NAME}.hex"

        COMMAND /bin/sh -c
        "\"${CMAKE_OBJDUMP}\" --source --all-headers --demangle -M xw --line-numbers --wide \"$<TARGET_FILE:${TARGET_NAME}>\" > \"${TARGET_NAME}.lst\""

        COMMAND /bin/sh -c
        "\"${CMAKE_OBJDUMP}\" -S -d -M xw \"$<TARGET_FILE:${TARGET_NAME}>\" > \"${TARGET_NAME}.asm\""

        COMMAND "${CMAKE_COMMAND}"
        -E copy
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.elf"

        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating HEX/LST/ASM files and displaying memory usage..."
)

set(EANALY_EXE "/bin/eanaly")
if(EXISTS "${EANALY_EXE}")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND "${EANALY_EXE}"
            "$<TARGET_FILE:${TARGET_NAME}>"

            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Running WCH post-analysis..."
    )
endif()
