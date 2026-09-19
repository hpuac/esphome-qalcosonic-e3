"""Axioma QALCOSONIC E3 optical M-Bus readout."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, button, sensor, text_sensor, uart
from esphome.const import CONF_ID, CONF_DISABLED_BY_DEFAULT

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "button"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("qalcosonic_e3")
QalcosonicE3 = ns.class_("QalcosonicE3", cg.PollingComponent, uart.UARTDevice)
ReadNowButton = ns.class_("ReadNowButton", button.Button)


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
            state_class="total",
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
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
            cv.Optional("readout_successful"): binary_sensor.binary_sensor_schema(
                device_class="connectivity", entity_category="diagnostic"
            ),
            cv.Optional("read_now"): button.button_schema(ReadNowButton, entity_category="config", icon="mdi:refresh"),
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
