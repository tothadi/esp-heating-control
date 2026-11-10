idf_component_register(SRCS "hc_settings.c" "hc_ntc.c" "hc_zones.c" "hc_flow.c" "hc_schedule.c" "hc_ui.c"
                            "hc_core.c" "hc_json.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hc_drivers opentherm espressif__cjson
                       PRIV_REQUIRES nvs_flash esp_timer esp_driver_gpio lwip log)
