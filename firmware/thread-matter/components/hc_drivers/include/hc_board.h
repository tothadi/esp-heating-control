#pragma once
// ESP Heating Control - logic board pin map (ESP32-S3-WROOM-1-N16R8). Source: hardware/build_logic.py.
#include "driver/gpio.h"

// Front panel
#define HC_PIN_BUTTON        GPIO_NUM_0    // BOOT strap, 10 kOhm pull-up, pressed = low
#define HC_PIN_LED_PUMP1     GPIO_NUM_4
#define HC_PIN_LED_PUMP2     GPIO_NUM_5
#define HC_PIN_LED_STATUS    GPIO_NUM_6
// I2C: MCP23017 0x20/0x21, ADS1115 0x48/0x49 (power/IO board, via the FFC), SSD1306 0x3C, DS3231M 0x68
#define HC_PIN_I2C_SDA       GPIO_NUM_8
#define HC_PIN_I2C_SCL       GPIO_NUM_18
#define HC_PIN_RTC_INT       GPIO_NUM_7    // DS3231 INT/SQW (unused)
#define HC_PIN_MCP_INT       GPIO_NUM_15   // MCP23017 INT (unused: inputs are polled)
// OpenTherm master interface (power/IO board). TX high = line idle; RX low = boiler current high (inverted).
#define HC_PIN_OT_TX         GPIO_NUM_17
#define HC_PIN_OT_RX         GPIO_NUM_16
// ESP32-H2 co-processor
#define HC_PIN_H2_TX         GPIO_NUM_10   // S3 TX -> H2 RXD0 (GPIO23)
#define HC_PIN_H2_RX         GPIO_NUM_11   // S3 RX <- H2 TXD0 (GPIO24)
#define HC_PIN_H2_EN         GPIO_NUM_12   // H2 EN (10 kOhm pull-up, 1 uF)
#define HC_PIN_H2_BOOT       GPIO_NUM_13   // H2 GPIO9 (10 kOhm pull-up)
// Wi-Fi / 802.15.4 coexistence (PTA), S3 = leader, H2 = follower
#define HC_PIN_PTA_REQUEST   GPIO_NUM_14   // <- H2 GPIO2
#define HC_PIN_PTA_PRIORITY  GPIO_NUM_21   // <- H2 GPIO3
#define HC_PIN_PTA_GRANT     GPIO_NUM_47   // -> H2 GPIO4
#define HC_PIN_PTA_TX_LINE   GPIO_NUM_48   // -> H2 GPIO5
// W5500 Ethernet (power/IO board, via the FFC); no reset line
#define HC_PIN_ETH_INT       GPIO_NUM_38
#define HC_PIN_ETH_SCK       GPIO_NUM_39
#define HC_PIN_ETH_MOSI      GPIO_NUM_40
#define HC_PIN_ETH_MISO      GPIO_NUM_41
#define HC_PIN_ETH_CS        GPIO_NUM_42

#define HC_ADDR_MCP_RELAYS   0x20   // GPA0..7 = Z1..Z8, GPB0..1 = P1..P2 (through ULN2003A)
#define HC_ADDR_MCP_IO       0x21   // GPA0..7 = TERM8..TERM1, GPB0..7 = zone LED 1..8
#define HC_ADDR_ADS_A        0x48   // AIN3..AIN0 = NTC1..NTC4
#define HC_ADDR_ADS_B        0x49   // AIN3..AIN0 = NTC5..NTC8
#define HC_ADDR_OLED         0x3C
#define HC_ADDR_RTC          0x68
