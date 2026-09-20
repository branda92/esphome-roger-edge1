import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ENTITY_CATEGORY
from . import roger_edge1_ns, PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
RogerSelect = roger_edge1_ns.class_("RogerSelect", select.Select)
CONFIG_SCHEMA = select.select_schema(RogerSelect, icon="mdi:tune").extend(PARENT_SCHEMA).extend({
    cv.Required("parameter"): cv.All(cv.int_, cv.one_of(38, 80, int=True)),
    cv.Optional(CONF_ENTITY_CATEGORY, default="config"): cv.entity_category,
})


async def to_code(config):
    var = await select.new_select(config, options=["0", "1"])
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_parameter(config["parameter"]))
    cg.add(parent.set_parameter_select(config["parameter"], var))
