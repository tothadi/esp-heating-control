"""Co-processor actions as buttons."""

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_RESTART, ENTITY_CATEGORY_CONFIG, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_H2_COPROCESSOR_ID, H2Coprocessor, h2_coprocessor_ns

DEPENDENCIES = ["h2_coprocessor"]

CONF_PERMIT_JOIN = "permit_join"
CONF_STOP_JOIN = "stop_join"
CONF_RESET = "reset"
CONF_REFRESH = "refresh"

CoprocessorButton = h2_coprocessor_ns.class_("CoprocessorButton", button.Button)
ButtonAction = h2_coprocessor_ns.enum("ButtonAction")
ACTIONS = {
    CONF_PERMIT_JOIN: ButtonAction.BUTTON_PERMIT_JOIN,
    CONF_STOP_JOIN: ButtonAction.BUTTON_STOP_JOIN,
    CONF_RESET: ButtonAction.BUTTON_RESET,
    CONF_REFRESH: ButtonAction.BUTTON_REFRESH,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_H2_COPROCESSOR_ID): cv.use_id(H2Coprocessor),
        cv.Optional(CONF_PERMIT_JOIN): button.button_schema(
            CoprocessorButton, entity_category=ENTITY_CATEGORY_CONFIG, icon="mdi:link-plus"
        ),
        cv.Optional(CONF_STOP_JOIN): button.button_schema(
            CoprocessorButton, entity_category=ENTITY_CATEGORY_CONFIG, icon="mdi:link-off"
        ),
        cv.Optional(CONF_RESET): button.button_schema(
            CoprocessorButton,
            device_class=DEVICE_CLASS_RESTART,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_REFRESH): button.button_schema(
            CoprocessorButton, entity_category=ENTITY_CATEGORY_DIAGNOSTIC, icon="mdi:refresh"
        ),
    }
)


async def to_code(config):
    for key, action in ACTIONS.items():
        if conf := config.get(key):
            b = await button.new_button(conf)
            await cg.register_parented(b, config[CONF_H2_COPROCESSOR_ID])
            cg.add(b.set_action(action))
