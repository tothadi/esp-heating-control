# ESP Heating Control - "Thread/Matter mode" firmware for the ESP32-S3 (heating control + Thread border router +
# Matter controller). Needs ESP-IDF v5.5.5 and esp-matter release/v1.6 (ESP_MATTER_PATH), see README.md.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of the esp-matter repo (release/v1.6)")
endif()
set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# RCP image to embed: build directory of ../h2-rcp (override with the environment variable HC_RCP_BUILD_DIR).
# It is passed to esp_rcp_update through an extra sdkconfig defaults file (CONFIG_RCP_SRC_DIR).
if(DEFINED ENV{HC_RCP_BUILD_DIR})
    set(HC_RCP_BUILD_DIR $ENV{HC_RCP_BUILD_DIR})
else()
    set(HC_RCP_BUILD_DIR ${CMAKE_CURRENT_LIST_DIR}/../h2-rcp/build)
endif()
get_filename_component(HC_RCP_BUILD_DIR "${HC_RCP_BUILD_DIR}" ABSOLUTE)
if(NOT EXISTS "${HC_RCP_BUILD_DIR}/flasher_args.json")
    message(FATAL_ERROR "RCP build not found in ${HC_RCP_BUILD_DIR}: build ../h2-rcp first (see README.md)")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/sdkconfig.rcp "CONFIG_RCP_SRC_DIR=\"${HC_RCP_BUILD_DIR}\"\n")
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
list(APPEND SDKCONFIG_DEFAULTS ${CMAKE_BINARY_DIR}/sdkconfig.rcp)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(heating-thread-matter)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual;-Wno-error=unused-result" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-D_NO_GENERIC_THREAD_NETWORK_COMMISSIONING_DRIVER_" APPEND)
# As in esp-matter's controller example: connectedhomeip has format-string warnings that -Werror would turn into
# errors on this toolchain (e.g. %u with uint32_t in CHIPDeviceController.cpp).
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security;-Wformat=0" APPEND)
