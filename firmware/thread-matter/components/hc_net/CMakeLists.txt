idf_component_register(SRCS "hc_net.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_netif espressif__cjson
                       PRIV_REQUIRES hc_drivers heating esp_eth esp_wifi esp_driver_spi esp_driver_gpio nvs_flash
                                     chip esp_matter log)
