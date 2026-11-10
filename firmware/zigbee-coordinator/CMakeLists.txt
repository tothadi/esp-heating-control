# ESP Heating Control - Zigbee coordinator firmware for the ESP32-H2 co-processor.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build what main depends on (as the Espressif examples do).
idf_build_set_property(MINIMAL_BUILD ON)

project(zigbee-coordinator)
