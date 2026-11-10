idf_component_register(SRCS "zb_coordinator.c"
                       PRIV_INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_timer cp_link)
