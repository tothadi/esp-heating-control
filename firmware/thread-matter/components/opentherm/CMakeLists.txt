idf_component_register(SRCS "ot_phy.c" "ot_master.c"
                       INCLUDE_DIRS "include"
                       REQUIRES hc_drivers
                       PRIV_REQUIRES esp_driver_rmt esp_driver_gpio esp_timer freertos log)
