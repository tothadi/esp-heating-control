idf_component_register(SRCS "app_main.cpp" "hc_thread.cpp" "hc_matter.cpp" "hc_mqtt.cpp"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "${ESP_MATTER_PATH}/examples/common/utils"
                       REQUIRES heating hc_net hc_drivers opentherm
                       PRIV_REQUIRES mqtt spiffs openthread esp_coex nvs_flash espressif__cjson esp_matter esp_matter_controller
                                     esp_matter_console chip espressif__esp_rcp_update esp_timer
                       LDFRAGMENTS "linker.lf")
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
