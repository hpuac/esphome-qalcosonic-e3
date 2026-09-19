# ESPHome QALCOSONIC E3

ESPHome external component for reading Axioma QALCOSONIC E3 heat meters via M-Bus over UART.

Read energy, volume, power, flow, and temperatures directly into Home Assistant,
with meter diagnostics and a manual read button. The component uses the optical
M-Bus interface through a TTL optical read/write head.

## Hardware

The tested setup uses an **Axioma QALCOSONIC E3**, a **Waveshare ESP32-C6-Zero**
with 8 MB flash, and a **TTL optical read/write head** powered from 3.3 V.

- [TTL IR read/write head](https://www.ebay.de/itm/356650595031)
- [Waveshare ESP32-C6-Zero](https://www.amazon.de/dp/B0F12PRH9G)

Other ESPHome-supported boards and compatible TTL heads should work with the
appropriate board configuration and wiring. The head must support both sending
and receiving, with logic levels compatible with the board. Communication uses
**2400 baud, 8E1**. The decoded data is specific to the QALCOSONIC E3.

## Installation and configuration

The following configuration enables the main measurements, readout
status, and manual read button:

```yaml
esphome:
  name: heat-meter

esp32:
  variant: esp32c6
  flash_size: 8MB
  framework:
    type: esp-idf

logger:

api:
  encryption:
    key: !secret heat_meter__encryption_key

ota:
  - platform: esphome
    encryption:

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

external_components:
  - source:
      type: git
      url: https://github.com/hpuac/esphome-qalcosonic-e3.git
    components: [qalcosonic_e3]

uart:
  id: mbus_uart
  rx_pin: GPIO2
  tx_pin: GPIO3
  baud_rate: 2400
  data_bits: 8
  parity: EVEN
  stop_bits: 1
  rx_buffer_size: 512

qalcosonic_e3:
  uart_id: mbus_uart
  update_interval: 2min
  energy:
    name: "Energy"
  volume:
    name: "Volume"
  power:
    name: "Power"
  flow:
    name: "Flow"
  flow_temperature:
    name: "Flow Temperature"
  return_temperature:
    name: "Return Temperature"
  temperature_difference:
    name: "Temperature Difference"
  readout_successful:
    name: "Readout Successful"
  read_now:
    name: "Read Now"
```

Add your Wi-Fi credentials and API encryption key to `secrets.yaml`, and adjust
the board and GPIO pins for your hardware.

[The full example](examples/esp32-c6.yaml) includes every optional entity.
Each nested entity can be omitted, renamed, or configured with normal ESPHome
entity options (including `id`, filters for numeric sensors, and
`disabled_by_default: false`). Omitted entities are not created; the defaults
below apply when an entity is configured. Diagnostic entities are disabled in Home Assistant by default, except for
`readout_successful`. Enable them in Home Assistant or override the default:

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  error_code:
    name: "Error Code"
    disabled_by_default: false
```

The component requires both UART pins and validates 2400 baud, 8 data bits,
even parity, and 1 stop bit. Use a dedicated UART with a 512-byte RX buffer.

## Wiring

| ESP32      | TTL optical head |
| ---------- | ---------------- |
| 3.3 V      | VCC              |
| GND        | GND              |
| GPIO2 (RX) | TX → ESP RX      |
| GPIO3 (TX) | RX ← ESP TX      |

Pins are configurable. This is M-Bus, **not SML**. The component reads optical M-Bus through a
TTL head. The wired M-Bus interface uses the same high-level
telegram format but needs a proper M-Bus master/transceiver; never connect the
meter's M-Bus wires directly to ESP GPIO.

## Optical interface and polling

The optical interface goes inactive after approximately **5 minutes without
communication**. After it sleeps, it requires local activation at the meter before communication resumes.
The default **2-minute** polling interval intentionally keeps it awake. Use an
interval below the inactivity timeout for continuous availability. Battery-current
consumption has not been measured, so no battery-life impact is quantified.

The first reading starts about 10 seconds after startup. Use `update_interval`
to change the polling interval or `read_now` to request a reading manually.
The component only reads meter data; it does not change meter settings.

## Entities

| Entity key                     | Type          | Unit | Default  | Category   |
| ------------------------------ | ------------- | ---- | -------- | ---------- |
| `energy`                       | Sensor        | MWh  | Enabled  | Normal     |
| `volume`                       | Sensor        | m³   | Enabled  | Normal     |
| `power`                        | Sensor        | kW   | Enabled  | Normal     |
| `flow`                         | Sensor        | m³/h | Enabled  | Normal     |
| `flow_temperature`             | Sensor        | °C   | Enabled  | Normal     |
| `return_temperature`           | Sensor        | °C   | Enabled  | Normal     |
| `temperature_difference`       | Sensor        | K    | Enabled  | Normal     |
| `error_code`                   | Sensor        | —    | Disabled | Diagnostic |
| `battery_operating_duration`   | Sensor        | d    | Disabled | Diagnostic |
| `operating_time_without_error` | Sensor        | d    | Disabled | Diagnostic |
| `protocol_version`             | Sensor        | —    | Disabled | Diagnostic |
| `meter_datetime`               | Text sensor   | —    | Disabled | Diagnostic |
| `error_start`                  | Text sensor   | —    | Disabled | Diagnostic |
| `serial_number`                | Text sensor   | —    | Disabled | Diagnostic |
| `manufacturer`                 | Text sensor   | —    | Disabled | Diagnostic |
| `readout_successful`           | Binary sensor | —    | Enabled  | Diagnostic |
| `readout_failures`             | Sensor        | —    | Disabled | Diagnostic |
| `read_now`                     | Button        | —    | Enabled  | Config     |

Battery operating duration is **elapsed operating time**, not percentage or
remaining battery life.

Meter error code and error start describe the meter itself. Error start is
`No error` when the error code is zero; otherwise dates use `YYYY-MM-DD HH:MM`,
without timezone conversion. Missing records leave existing states unchanged.

`readout_successful` reports whether the last read succeeded and stays unknown
until the first result. A read times out after 2 seconds. `readout_failures`
counts failed reads since boot and resets when the ESP restarts. Missing
readings do not clear previously reported measurements.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, tests, and formatting.

## License

[MIT](LICENSE).
