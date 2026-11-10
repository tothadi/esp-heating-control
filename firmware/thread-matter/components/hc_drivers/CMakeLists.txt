idf_component_register(SRCS "hc_i2c.c" "hc_mcp23017.c" "hc_ads1115.c" "hc_ds3231.c" "hc_ssd1306.c" "hc_font.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio
                       PRIV_REQUIRES freertos log)
