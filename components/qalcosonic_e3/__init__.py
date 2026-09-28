"""Axioma QALCOSONIC E3 optical M-Bus readout."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, sensor, switch, text_sensor, uart
from esphome.const import CONF_DISABLED_BY_DEFAULT, CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "button", "switch"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("qalcosonic_e3")
QalcosonicE3 = ns.class_("QalcosonicE3", cg.PollingComponent, uart.UARTDevice)
ReadNowButton = ns.class_("ReadNowButton", button.Button)
AutomaticReadoutSwitch = ns.class_("AutomaticReadoutSwitch", switch.Switch, cg.Component)


def diagnostic(schema):
    return schema.extend({cv.Optional(CONF_DISABLED_BY_DEFAULT, default=True): cv.boolean})


SENSORS = {
    "energy": sensor.sensor_schema(
        accuracy_decimals=3,
        unit_of_measurement="MWh",
        device_class="energy",
        state_class="total_increasing",
    ),
    "volume": sensor.sensor_schema(
        accuracy_decimals=3,
        unit_of_measurement="m³",
        device_class="water",
        state_class="total_increasing",
    ),
    "power": sensor.sensor_schema(
        accuracy_decimals=3,
        unit_of_measurement="kW",
        device_class="power",
        state_class="measurement",
    ),
    "flow": sensor.sensor_schema(
        accuracy_decimals=3,
        unit_of_measurement="m³/h",
        icon="mdi:waves-arrow-right",
        state_class="measurement",
    ),
    "flow_temperature": sensor.sensor_schema(
        accuracy_decimals=2,
        unit_of_measurement="°C",
        device_class="temperature",
        state_class="measurement",
    ),
    "return_temperature": sensor.sensor_schema(
        accuracy_decimals=2,
        unit_of_measurement="°C",
        device_class="temperature",
        state_class="measurement",
    ),
    "temperature_difference": sensor.sensor_schema(
        accuracy_decimals=2,
        unit_of_measurement="K",
        icon="mdi:thermometer-lines",
        state_class="measurement",
    ),
    "error_code": diagnostic(
        sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:alert-circle-outline",
            entity_category="diagnostic",
        )
    ),
    "battery_operating_duration": diagnostic(
        sensor.sensor_schema(
            accuracy_decimals=1,
            unit_of_measurement="d",
            icon="mdi:battery-clock",
            entity_category="diagnostic",
        )
    ),
    "operating_time_without_error": diagnostic(
        sensor.sensor_schema(
            accuracy_decimals=1,
            unit_of_measurement="d",
            icon="mdi:timer-outline",
            entity_category="diagnostic",
        )
    ),
    "protocol_version": diagnostic(
        sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:information-outline",
            entity_category="diagnostic",
        )
    ),
    "readout_failures": diagnostic(
        sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:counter",
            state_class="measurement",
            entity_category="diagnostic",
        )
    ),
}
TEXT_SENSORS = {
    "meter_datetime": diagnostic(
        text_sensor.text_sensor_schema(
            entity_category="diagnostic",
            icon="mdi:clock-outline",
        )
    ),
    "error_start": diagnostic(
        text_sensor.text_sensor_schema(
            entity_category="diagnostic",
            icon="mdi:clock-alert-outline",
        )
    ),
    "serial_number": diagnostic(
        text_sensor.text_sensor_schema(
            entity_category="diagnostic",
            icon="mdi:identifier",
        )
    ),
    "manufacturer": diagnostic(
        text_sensor.text_sensor_schema(
            entity_category="diagnostic",
            icon="mdi:factory",
        )
    ),
}
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(QalcosonicE3),
            cv.Optional("unavailable_after_failures", default=0): cv.uint32_t,
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
            cv.Optional("readout_successful"): binary_sensor.binary_sensor_schema(
                device_class="connectivity", entity_category="diagnostic"
            ),
            cv.Optional("read_now"): button.button_schema(ReadNowButton, entity_category="config", icon="mdi:refresh"),
            cv.Optional("automatic_readout"): switch.switch_schema(
                AutomaticReadoutSwitch,
                block_inverted=True,
                default_restore_mode="ALWAYS_ON",
                entity_category="config",
                icon="mdi:autorenew",
            ).extend(cv.COMPONENT_SCHEMA),
        }
    )
    .extend(cv.polling_component_schema("2min"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "qalcosonic_e3",
    baud_rate=2400,
    data_bits=8,
    parity="EVEN",
    stop_bits=1,
    require_rx=True,
    require_tx=True,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_unavailable_after_failures(config["unavailable_after_failures"]))
    for key in SENSORS:
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(getattr(var, f"set_{key}")(entity))
    for key in TEXT_SENSORS:
        if key in config:
            entity = await text_sensor.new_text_sensor(config[key])
            cg.add(getattr(var, f"set_{key}")(entity))
    if "readout_successful" in config:
        entity = await binary_sensor.new_binary_sensor(config["readout_successful"])
        cg.add(var.set_readout_successful(entity))
    if "read_now" in config:
        entity = await button.new_button(config["read_now"])
        cg.add(entity.set_parent(var))
    if "automatic_readout" in config:
        entity = await switch.new_switch(config["automatic_readout"])
        await cg.register_component(entity, config["automatic_readout"])
        cg.add(entity.set_parent(var))
