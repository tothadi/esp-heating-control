# ESP Heating Control - OpenThread RCP firmware for the ESP32-H2 co-processor ("Thread/Matter mode").
# Based on the ESP-IDF v5.5.5 example examples/openthread/ot_rcp (CC0), configured for this board.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
# The binary must be called esp_ot_rcp.bin: esp_rcp_update (create_ota_image.py) looks for that name.
project(esp_ot_rcp)
