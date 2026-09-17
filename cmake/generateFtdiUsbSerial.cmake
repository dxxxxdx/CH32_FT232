if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

string(TIMESTAMP FTDI_USB_SERIAL_SUFFIX "%y%m%d%H%M%S" UTC)
foreach(_serial_index RANGE 0 11)
    string(SUBSTRING "${FTDI_USB_SERIAL_SUFFIX}" ${_serial_index} 1
            FTDI_USB_SERIAL_CHAR_${_serial_index})
endforeach()

get_filename_component(_output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_directory}")
configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/ftdiUsbBuildSerial.h.in"
        "${OUTPUT_FILE}"
        @ONLY
)

message(STATUS "FTDI USB serial = CH32_FTDI_${FTDI_USB_SERIAL_SUFFIX}")
