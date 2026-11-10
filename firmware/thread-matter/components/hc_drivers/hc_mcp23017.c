// MCP23017 (IOCON.BANK = 0 register map). 0x20: all outputs (relays, pumps). 0x21: GPA inputs, GPB outputs.
#include "hc_drivers.h"
#include "hc_board.h"
#include "hc_i2c_priv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define REG_IODIRA 0x00
#define REG_GPIOA  0x12
#define REG_OLATA  0x14

static uint16_t s_olat[2];               // output latches (A = low byte)
static SemaphoreHandle_t s_lock;

static int idx(uint8_t addr) { return addr == HC_ADDR_MCP_RELAYS ? 0 : 1; }

static esp_err_t write_regs(uint8_t addr)
{
    const uint16_t iodir = addr == HC_ADDR_MCP_RELAYS ? 0x0000 : 0x00FF;
    uint8_t a[3] = {REG_IODIRA, iodir & 0xFF, iodir >> 8};
    uint16_t o = s_olat[idx(addr)];
    uint8_t b[3] = {REG_OLATA, o & 0xFF, o >> 8};
    esp_err_t err = hc_i2c_write(addr, b, 3);   // latches first, so outputs come up in the right state
    if (err == ESP_OK) err = hc_i2c_write(addr, a, 3);
    return err;
}

esp_err_t hc_mcp_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_olat[0] = s_olat[1] = 0;
    esp_err_t e1 = write_regs(HC_ADDR_MCP_RELAYS);
    esp_err_t e2 = write_regs(HC_ADDR_MCP_IO);
    return e1 != ESP_OK ? e1 : e2;
}

esp_err_t hc_mcp_refresh(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t e1 = write_regs(HC_ADDR_MCP_RELAYS);
    esp_err_t e2 = write_regs(HC_ADDR_MCP_IO);
    xSemaphoreGive(s_lock);
    return e1 != ESP_OK ? e1 : e2;
}

esp_err_t hc_mcp_write_pin(uint8_t addr, uint8_t pin, bool level)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t *o = &s_olat[idx(addr)];
    if (level) *o |= 1u << pin; else *o &= ~(1u << pin);
    uint8_t reg = pin < 8 ? REG_OLATA : REG_OLATA + 1;
    uint8_t buf[2] = {reg, pin < 8 ? (*o & 0xFF) : (*o >> 8)};
    esp_err_t err = hc_i2c_write(addr, buf, 2);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t hc_mcp_read_gpioa(uint8_t addr, uint8_t *value)
{
    uint8_t reg = REG_GPIOA;
    return hc_i2c_write_read(addr, &reg, 1, value, 1);
}
