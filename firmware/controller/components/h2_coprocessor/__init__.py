"""ESP Heating Control - link to the ESP32-H2 Zigbee co-processor (firmware/zigbee-coordinator).

Protocol: docs/coprocessor-protocol.md. Usage: firmware/controller/coprocessor.yaml.
"""

from esphome import pins
import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_RESET_PIN

CODEOWNERS = ["@tothadi"]
DEPENDENCIES = ["uart"]

CONF_H2_COPROCESSOR_ID = "h2_coprocessor_id"
CONF_PERMIT_JOIN_DURATION = "permit_join_duration"

h2_coprocessor_ns = cg.esphome_ns.namespace("h2_coprocessor")
H2Coprocessor = h2_coprocessor_ns.class_("H2Coprocessor", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(H2Coprocessor),
            # S3 IO12 -> H2 EN. Kept as an input except during a reset pulse.
            cv.Optional(CONF_RESET_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_PERMIT_JOIN_DURATION, default="180s"): cv.All(
                cv.positive_time_period_seconds,
                cv.Range(min=cv.TimePeriod(seconds=1), max=cv.TimePeriod(seconds=254)),
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "h2_coprocessor",
    baud_rate=115200,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if reset_pin := config.get(CONF_RESET_PIN):
        pin = await cg.gpio_pin_expression(reset_pin)
        cg.add(var.set_reset_pin(pin))
    cg.add(var.set_permit_join_duration(config[CONF_PERMIT_JOIN_DURATION].total_seconds))
