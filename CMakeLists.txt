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

# 留空时由 UartForwardConfig.h 选择默认 PA23；自定义构建脚本
# 可传 -DUART_FORWARD_PORT_SELECT=PB67，该值会传递到所有 C 编译单元。
set(UART_FORWARD_PORT_SELECT "" CACHE STRING
        "UART forwarding port: empty/PA23 or PB67")
set_property(CACHE UART_FORWARD_PORT_SELECT PROPERTY STRINGS "" PA23 PB67)

set(UART_FORWARD_PORT_DEFINITION "")
if(UART_FORWARD_PORT_SELECT STREQUAL "" OR
   UART_FORWARD_PORT_SELECT STREQUAL "PA23")
    message(STATUS "UART forwarding port = PA23 (default)")
elseif(UART_FORWARD_PORT_SELECT STREQUAL "PB67")
    set(UART_FORWARD_PORT_DEFINITION
            UART_FORWARD_PORT=UART_FORWARD_PORT_PB67)
    message(STATUS "UART forwarding port = PB67")
else()
    message(FATAL_ERROR
            "UART_FORWARD_PORT_SELECT must be PA23 or PB67, got: ${UART_FORWARD_PORT_SELECT}")
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
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/cmake-build-.*/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/build/.*")
    list(FILTER ${_source_list} EXCLUDE REGEX ".*/CMakeFiles/.*")
endforeach()

set_source_files_properties(${PROJECT_ASM_SOURCES} PROPERTIES LANGUAGE ASM)

add_executable(${TARGET_NAME}
        ${PROJECT_C_SOURCES}
        ${PROJECT_CXX_SOURCES}
        ${PROJECT_ASM_SOURCES}
)

set_target_properties(${TARGET_NAME} PROPERTIES
        OUTPUT_NAME "${TARGET_NAME}"
        SUFFIX ".elf"
)

# =========================================================================
# 6. Include directories
# =========================================================================
target_include_directories(${TARGET_NAME} PRIVATE
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
        ${UART_FORWARD_PORT_DEFINITION}
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
    list(FILTER ALL_LIB_FILES EXCLUDE REGEX ".*/cmake-build-.*/.*")
    list(FILTER ALL_LIB_FILES EXCLUDE REGEX ".*/build/.*")
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
