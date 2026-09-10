import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, light, number, select, sensor, switch, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ['binary_sensor']
AUTO_LOAD = ['binary_sensor', 'sensor', 'text_sensor']

cyrus_ble_ns = cg.esphome_ns.namespace('cyrus_ble')
CyrusBleComponent = cyrus_ble_ns.class_('CyrusBleComponent', cg.Component)

CONF_LED = "led"
CONF_STATUS_SENSOR = "status_sensor"
CONF_CONNECTED_SENSOR = "connected_sensor"
CONF_READY_SENSOR = "ready_sensor"
CONF_MUTED_SENSOR = "muted_sensor"
CONF_HEADPHONES_SENSOR = "headphones_sensor"
CONF_AV_DIRECT_SENSOR = "av_direct_sensor"
CONF_VOLUME_SENSOR = "volume_sensor"
CONF_BALANCE_SENSOR = "balance_sensor"
CONF_SOURCE_SENSOR = "source_sensor"
CONF_MODEL_SENSOR = "model_sensor"
CONF_SW_VERSION_SENSOR = "sw_version_sensor"
CONF_SERIAL_SENSOR = "serial_sensor"
CONF_VOLUME_NUMBER = "volume_number"
CONF_BALANCE_NUMBER = "balance_number"
CONF_SOURCE_SELECT = "source_select"
CONF_MUTE_SWITCH = "mute_switch"
CONF_AV_DIRECT_SWITCH = "av_direct_switch"

_BINARY_SENSORS = {
    CONF_STATUS_SENSOR: "set_status_sensor",
    CONF_CONNECTED_SENSOR: "set_connected_binary_sensor",
    CONF_READY_SENSOR: "set_ready_binary_sensor",
    CONF_MUTED_SENSOR: "set_muted_binary_sensor",
    CONF_HEADPHONES_SENSOR: "set_headphones_binary_sensor",
    CONF_AV_DIRECT_SENSOR: "set_av_direct_binary_sensor",
}

_SENSORS = {
    CONF_VOLUME_SENSOR: "set_volume_sensor",
    CONF_BALANCE_SENSOR: "set_balance_sensor",
}

_TEXT_SENSORS = {
    CONF_SOURCE_SENSOR: "set_source_text_sensor",
    CONF_MODEL_SENSOR: "set_model_text_sensor",
    CONF_SW_VERSION_SENSOR: "set_sw_version_text_sensor",
    CONF_SERIAL_SENSOR: "set_serial_text_sensor",
}

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(CyrusBleComponent),
    cv.Optional(CONF_LED): cv.use_id(light.LightState),
    **{cv.Optional(k): cv.use_id(binary_sensor.BinarySensor) for k in _BINARY_SENSORS},
    **{cv.Optional(k): cv.use_id(sensor.Sensor) for k in _SENSORS},
    **{cv.Optional(k): cv.use_id(text_sensor.TextSensor) for k in _TEXT_SENSORS},
    cv.Optional(CONF_VOLUME_NUMBER): cv.use_id(number.Number),
    cv.Optional(CONF_BALANCE_NUMBER): cv.use_id(number.Number),
    cv.Optional(CONF_SOURCE_SELECT): cv.use_id(select.Select),
    cv.Optional(CONF_MUTE_SWITCH): cv.use_id(switch.Switch),
    cv.Optional(CONF_AV_DIRECT_SWITCH): cv.use_id(switch.Switch),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_LED in config:
        led = await cg.get_variable(config[CONF_LED])
        cg.add(var.set_led(led))

    for key, setter in _BINARY_SENSORS.items():
        if key in config:
            sens = await cg.get_variable(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in _SENSORS.items():
        if key in config:
            sens = await cg.get_variable(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter in _TEXT_SENSORS.items():
        if key in config:
            sens = await cg.get_variable(config[key])
            cg.add(getattr(var, setter)(sens))

    for key, setter, cls in (
        (CONF_VOLUME_NUMBER, "set_volume_number", number.Number),
        (CONF_BALANCE_NUMBER, "set_balance_number", number.Number),
        (CONF_SOURCE_SELECT, "set_source_select", select.Select),
        (CONF_MUTE_SWITCH, "set_mute_switch", switch.Switch),
        (CONF_AV_DIRECT_SWITCH, "set_av_direct_switch", switch.Switch),
    ):
        if key in config:
            ent = await cg.get_variable(config[key])
            cg.add(getattr(var, setter)(ent))
