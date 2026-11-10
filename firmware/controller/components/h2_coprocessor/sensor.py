"""Co-processor device values as ESPHome sensors, mapped by device id."""

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIMEOUT,
    CONF_TYPE,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from . import CONF_H2_COPROCESSOR_ID, H2Coprocessor, h2_coprocessor_ns

DEPENDENCIES = ["h2_coprocessor"]

CONF_DEVICE = "device"

CoprocessorSensor = h2_coprocessor_ns.class_("CoprocessorSensor", sensor.Sensor)

# type -> (ZCL cluster, attribute) pairs it accepts, unit, device class
TYPES = {
    # room temperature: Temperature Measurement MeasuredValue, or a thermostat's LocalTemperature
    "temperature": ([(0x0402, 0x0000), (0x0201, 0x0000)], UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE),
    "humidity": ([(0x0405, 0x0000)], UNIT_PERCENT, DEVICE_CLASS_HUMIDITY),
    "local_temperature": ([(0x0201, 0x0000)], UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE),
    "setpoint": ([(0x0201, 0x0012)], UNIT_CELSIUS, DEVICE_CLASS_TEMPERATURE),
    "heating_demand": ([(0x0201, 0x0008)], UNIT_PERCENT, None),
}


def validate_device_id(value):
    """16 hex digits (as in the co-processor's device list), or empty = not mapped."""
    value = cv.string_strict(value).strip().lower().replace(":", "")
    if value == "":
        return value
    if len(value) != 16 or any(c not in "0123456789abcdef" for c in value):
        raise cv.Invalid("device must be 16 hex digits, e.g. 00124b0012345678 (or empty)")
    return value


def _type_schema(unit, device_class):
    kwargs = {"unit_of_measurement": unit, "accuracy_decimals": 1, "state_class": STATE_CLASS_MEASUREMENT}
    if device_class:
        kwargs["device_class"] = device_class
    return sensor.sensor_schema(CoprocessorSensor, **kwargs).extend(
        {
            cv.GenerateID(CONF_H2_COPROCESSOR_ID): cv.use_id(H2Coprocessor),
            cv.Required(CONF_DEVICE): validate_device_id,
            # After this long without a reading the sensor becomes NaN (unknown).
            cv.Optional(CONF_TIMEOUT, default="1h"): cv.positive_time_period_milliseconds,
        }
    )


CONFIG_SCHEMA = cv.typed_schema(
    {name: _type_schema(unit, dc) for name, (_, unit, dc) in TYPES.items()},
    default_type="temperature",
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_H2_COPROCESSOR_ID])
    var = await sensor.new_sensor(config)
    cg.add(var.set_device_id(config[CONF_DEVICE]))
    for cluster, attr in TYPES[config[CONF_TYPE]][0]:
        cg.add(var.add_match(cluster, attr))
    cg.add(var.set_timeout(config[CONF_TIMEOUT].total_milliseconds))
    cg.add(parent.register_sensor(var))
