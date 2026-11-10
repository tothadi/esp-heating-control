"""Co-processor status as text sensors."""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_H2_COPROCESSOR_ID, H2Coprocessor

DEPENDENCIES = ["h2_coprocessor"]

CONF_DEVICE_LIST = "device_list"
CONF_FIRMWARE = "firmware"
CONF_NETWORK = "network"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_H2_COPROCESSOR_ID): cv.use_id(H2Coprocessor),
        cv.Optional(CONF_DEVICE_LIST): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:format-list-bulleted"
        ),
        cv.Optional(CONF_FIRMWARE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:chip"
        ),
        cv.Optional(CONF_NETWORK): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:zigbee"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_H2_COPROCESSOR_ID])
    if conf := config.get(CONF_DEVICE_LIST):
        cg.add(parent.set_device_list_text_sensor(await text_sensor.new_text_sensor(conf)))
    if conf := config.get(CONF_FIRMWARE):
        cg.add(parent.set_firmware_text_sensor(await text_sensor.new_text_sensor(conf)))
    if conf := config.get(CONF_NETWORK):
        cg.add(parent.set_network_text_sensor(await text_sensor.new_text_sensor(conf)))
