idf_component_register(SRCS "cp_link.c" "cp_devtable.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_uart esp_timer nvs_flash)
