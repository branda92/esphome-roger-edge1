import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from . import PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
CONFIG_SCHEMA = cv.All(PARENT_SCHEMA.extend({
    cv.Optional("online"): binary_sensor.binary_sensor_schema(
        device_class="connectivity", entity_category="diagnostic",
    ),
    cv.Optional("ft1"): binary_sensor.binary_sensor_schema(icon="mdi:laser-pointer"),
    cv.Optional("ft2"): binary_sensor.binary_sensor_schema(icon="mdi:laser-pointer"),
}), cv.has_at_least_one_key("online", "ft1", "ft2"))


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    if "online" in config:
        var = await binary_sensor.new_binary_sensor(config["online"])
        cg.add(parent.set_online_sensor(var))
    for index, key in enumerate(("ft1", "ft2")):
        if key in config:
            var = await binary_sensor.new_binary_sensor(config[key])
            cg.add(parent.set_photocell_sensor(index, var))
