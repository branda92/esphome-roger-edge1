"""Roger EDGE1: verified EXP UART registers, one owner of the serial bus."""
import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_ADDRESS, CONF_UPDATE_INTERVAL, CONF_UART_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "cover", "button", "select"]
MULTI_CONF = True

roger_edge1_ns = cg.esphome_ns.namespace("roger_edge1")
RogerEdge1 = roger_edge1_ns.class_("RogerEdge1", cg.Component, uart.UARTDevice)
CONF_ROGER_EDGE1_ID = "roger_edge1_id"
CONF_RESPONSE_TIMEOUT = "response_timeout"
CONF_OFFLINE_TIMEOUT = "offline_timeout"
CONF_PARAMETER_UPDATE_INTERVAL = "parameter_update_interval"
CONF_INPUTS_UPDATE_INTERVAL = "inputs_update_interval"
CONF_INPUTS_TIMEOUT = "inputs_timeout"


def validate_timing(config):
    if config[CONF_OFFLINE_TIMEOUT].total_milliseconds <= (
        config[CONF_UPDATE_INTERVAL].total_milliseconds
        + config[CONF_RESPONSE_TIMEOUT].total_milliseconds
    ):
        raise cv.Invalid("offline_timeout must exceed update_interval + response_timeout")
    if config[CONF_INPUTS_TIMEOUT].total_milliseconds <= (
        config[CONF_INPUTS_UPDATE_INTERVAL].total_milliseconds
        + config[CONF_RESPONSE_TIMEOUT].total_milliseconds
    ):
        raise cv.Invalid("inputs_timeout must exceed inputs_update_interval + response_timeout")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(RogerEdge1),
        cv.Optional(CONF_ADDRESS, default=0x0A): cv.int_range(min=1, max=247),
        cv.Optional(CONF_UPDATE_INTERVAL, default="500ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(milliseconds=100), max=cv.TimePeriod(seconds=10)),
        ),
        cv.Optional(CONF_RESPONSE_TIMEOUT, default="200ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(milliseconds=50), max=cv.TimePeriod(seconds=1)),
        ),
        cv.Optional(CONF_OFFLINE_TIMEOUT, default="3s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=60)),
        ),
        cv.Optional(CONF_PARAMETER_UPDATE_INTERVAL, default="30s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=1), max=cv.TimePeriod(hours=24)),
        ),
        cv.Optional(CONF_INPUTS_UPDATE_INTERVAL, default="500ms"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(milliseconds=100), max=cv.TimePeriod(seconds=10)),
        ),
        cv.Optional(CONF_INPUTS_TIMEOUT, default="3s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(max=cv.TimePeriod(seconds=60)),
        ),
    }).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA),
    validate_timing,
)

_validate_uart = uart.final_validate_device_schema(
    "roger_edge1", baud_rate=115200, require_tx=True, require_rx=True,
    data_bits=8, parity="NONE", stop_bits=1,
)


def final_validate(config):
    _validate_uart(config)
    bus_id = str(config[CONF_UART_ID])

    def owns_uart(node):
        if isinstance(node, dict):
            if CONF_UART_ID in node and str(node[CONF_UART_ID]) == bus_id:
                return True
            return any(owns_uart(value) for value in node.values())
        if isinstance(node, list):
            return any(owns_uart(value) for value in node)
        return False

    full_config = fv.full_config.get()
    # Some shared-bus components (notably modbus) do not claim UART pins through
    # final_validate_device_schema. Check actual consumers as well as pin claims.
    for domain, settings in full_config.items():
        if domain != "roger_edge1" and owns_uart(settings):
            raise cv.Invalid(
                f"UART {bus_id} is used by {domain} and roger_edge1, but can only be used by one"
            )
    for bus in full_config.get("uart", []):
        if str(bus[CONF_ID]) == bus_id and bus.get("debug", {}).get("dummy_receiver", False):
            raise cv.Invalid("roger_edge1 requires uart.debug.dummy_receiver: false")
    return config


FINAL_VALIDATE_SCHEMA = final_validate

PARENT_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_ROGER_EDGE1_ID): cv.use_id(RogerEdge1),
})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_response_timeout(config[CONF_RESPONSE_TIMEOUT]))
    cg.add(var.set_offline_timeout(config[CONF_OFFLINE_TIMEOUT]))
    cg.add(var.set_parameter_update_interval(config[CONF_PARAMETER_UPDATE_INTERVAL]))
    cg.add(var.set_inputs_update_interval(config[CONF_INPUTS_UPDATE_INTERVAL]))
    cg.add(var.set_inputs_timeout(config[CONF_INPUTS_TIMEOUT]))
