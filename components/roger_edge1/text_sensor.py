import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from . import PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
TEXTS = {"state_1": 0, "state_2": 1, "last_result": 2}
CONFIG_SCHEMA = PARENT_SCHEMA.extend({
    cv.Optional(name): text_sensor.text_sensor_schema(
        icon="mdi:information-outline", **({"entity_category": "diagnostic"} if index == 2 else {}),
    ) for name, index in TEXTS.items()
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    for name, index in TEXTS.items():
        if name in config:
            var = await text_sensor.new_text_sensor(config[name])
            cg.add(parent.set_text_sensor(index, var))
