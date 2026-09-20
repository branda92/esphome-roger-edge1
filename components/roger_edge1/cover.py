import esphome.codegen as cg
from esphome.components import cover
from . import roger_edge1_ns, PARENT_SCHEMA, CONF_ROGER_EDGE1_ID

DEPENDENCIES = ["roger_edge1"]
RogerCover = roger_edge1_ns.class_("RogerCover", cover.Cover)
CONFIG_SCHEMA = cover.cover_schema(RogerCover, device_class="gate").extend(PARENT_SCHEMA)


async def to_code(config):
    var = await cover.new_cover(config)
    parent = await cg.get_variable(config[CONF_ROGER_EDGE1_ID])
    cg.add(var.set_parent(parent))
    cg.add(parent.set_cover(var))
