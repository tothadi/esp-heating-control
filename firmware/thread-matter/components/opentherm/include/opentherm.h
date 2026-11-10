#pragma once
// ESP Heating Control - OpenTherm master (OpenTherm Protocol Specification v2.2, 1 kbit/s Manchester).
// Physical layer: RMT on the TX pin, GPIO edge interrupts on the RX pin (ot_phy.c). Polling of the boiler and the
// values the controller writes (CH enable, DHW enable, flow setpoint) in ot_master.c.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // From the boiler (NaN / false if unknown or not supported)
    float t_boiler;          // ID25 flow temperature
    float t_return;          // ID28 return temperature
    float t_dhw;             // ID26 DHW temperature
    float t_outside;         // ID27 outdoor temperature (boiler's sensor)
    float rel_mod;           // ID17 relative modulation (%)
    float ch_pressure;       // ID18 CH water pressure (bar)
    float oem_fault_code;    // ID5 low byte
    bool fault, ch_active, dhw_active, flame;   // ID0 slave status flags
    bool comm_ok;            // a valid response within the last 10 s
    // Written by the controller
    bool ch_enable, dhw_enable;
    float t_set;             // ID1 control setpoint
} ot_state_t;

esp_err_t ot_master_start(int tx_gpio, int rx_gpio);
void ot_master_get(ot_state_t *out);
void ot_master_set_ch_enable(bool on);
void ot_master_set_dhw_enable(bool on);
void ot_master_set_t_set(float celsius);

// Physical layer (used by ot_master.c)
esp_err_t ot_phy_init(int tx_gpio, int rx_gpio);
// Sends one request frame and waits for the response (up to 800 ms). ESP_ERR_TIMEOUT / ESP_ERR_INVALID_RESPONSE.
esp_err_t ot_phy_transact(uint32_t request, uint32_t *response);

#ifdef __cplusplus
}
#endif
