import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from . import roger_edge1_ns, PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
RogerButton = roger_edge1_ns.class_("RogerButton", button.Button)
COMMANDS = {"STOP": 0x6801, "OPEN": 0x6802, "CLOSE": 0x6804, "PEDESTRIAN": 0x6810}
CONFIG_SCHEMA = button.button_schema(RogerButton).extend(PARENT_SCHEMA).extend({
    cv.Required("command"): cv.enum(COMMANDS, upper=True),
})


async def to_code(config):
    var = await button.new_button(config)
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_command(config["command"]))
