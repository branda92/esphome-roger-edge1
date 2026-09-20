import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from . import PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
SENSORS = {
    "raw_state": (0, None),
    "position_1_raw": (1, None),
    "position_2_raw": (2, None),
    "position_1": (3, "%"),
    "position_2": (4, "%"),
    "position": (5, "%"),
    "state_1": (6, None),
    "state_2": (7, None),
    "communication_errors": (8, None),
    "crc_errors": (9, None),
    "inputs_raw": (10, None),
    "input_aux_raw": (11, None),
}


def entity_schema(index, unit):
    kwargs = {"accuracy_decimals": 0, "icon": "mdi:gate"}
    if unit is not None:
        kwargs.update(unit_of_measurement=unit, state_class="measurement")
    else:
        kwargs["entity_category"] = "diagnostic"
    if index in (8, 9):
        kwargs.update(icon="mdi:counter", state_class="total_increasing")
    elif index >= 10:
        kwargs["icon"] = "mdi:code-brackets"
    return sensor.sensor_schema(**kwargs)


CONFIG_SCHEMA = PARENT_SCHEMA.extend({
    cv.Optional(name): entity_schema(*definition) for name, definition in SENSORS.items()
})


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    for name, (index, _) in SENSORS.items():
        if name in config:
            var = await sensor.new_sensor(config[name])
            cg.add(parent.set_sensor(index, var))
