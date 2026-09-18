# ESPHome QALCOSONIC E3

ESPHome external component for reading Axioma QALCOSONIC E3 heat meters via M-Bus over UART.

Read energy, volume, power, flow, and temperatures directly into Home Assistant,
with meter diagnostics and a manual read button. The component uses the optical
M-Bus interface through a TTL optical read/write head.

## Hardware

The setup described here uses an **Axioma QALCOSONIC E3** heat meter with
these parts:

| Part | Details | Product link |
| --- | --- | --- |
| TTL IR read/write head with ring magnet | Assembled RX/TX board with IR LED and phototransistor; 3.3–5 V supply, powered from **3.3 V** in this setup; 26 mm board outer diameter, 6.5 mm diode spacing (center to center), and 27/16/5 mm ring magnet | [eBay item 356650595031](https://www.ebay.de/itm/356650595031) |
| Waveshare ESP32-C6-Zero, without pin headers | ESP32-C6FH8 with 8 MB flash, USB-C, and onboard ceramic antenna; running ESPHome with ESP-IDF | [Amazon product B0F12PRH9G](https://www.amazon.de/dp/B0F12PRH9G) |

These parts were chosen to fit inside a very small, custom-designed 3D-printed
case. The optical head is the **TTL RX/TX read/write version**, supplied with a
27 mm ring magnet. Communication uses **2400 baud, 8E1**.

The component is not tied to these particular products. Other ESPHome-supported
boards and compatible TTL optical read/write heads should work when configured
with the same UART settings and appropriate pins. Use a head that supports both
transmitting requests and receiving responses, with UART logic levels compatible
with the board. Adjust the ESPHome board configuration and wiring for your hardware.

The meter protocol and decoded data set remain specific to the QALCOSONIC E3.

## Installation and configuration

Create `secrets.yaml` with `wifi_ssid` and `wifi_password`, then use the following
configuration. It enables the main measurements, readout status, and manual read
button:

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
ota:
  - platform: esphome
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

external_components:
  - source:
      type: git
      url: https://github.com/hpuac/esphome-qalcosonic-e3.git
      # ref: refs/pull/1/head  # Replace 1 with the PR number to test.
    components: [qalcosonic_e3]
    # refresh: 1min  # Enable while testing remote changes.

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

To test a pull request, uncomment `ref` and set its PR number. Uncomment
`refresh: 1min` to check for remote changes more frequently when running ESPHome.
Compile and flash again to apply changes to the device; this does not enable
automatic firmware updates.

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

For a local checkout, replace the Git source with:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [qalcosonic_e3]
```

The path is relative to your device YAML. ESPHome's supported external component
layout and UART integration are described in the
[external components documentation](https://esphome.io/components/external_components/)
and [UART developer documentation](https://developers.esphome.io/architecture/components/uart/).

## Wiring

| ESP32 | TTL optical head |
| --- | --- |
| 3.3 V | VCC |
| GND | GND |
| GPIO2 (RX) | TX → ESP RX |
| GPIO3 (TX) | RX ← ESP TX |

Pins are configurable. This is M-Bus, **not SML**. The component reads optical M-Bus through a
TTL head. The wired M-Bus interface uses the same high-level
telegram format but needs a proper M-Bus master/transceiver; never connect the
meter's M-Bus wires directly to ESP GPIO.

## Optical interface and polling

The optical interface goes inactive after approximately **5 minutes without
communication**. After it sleeps,
it requires local activation at the meter before communication resumes.
The default **2-minute** polling interval intentionally keeps it awake. Use an
interval below the inactivity timeout for continuous availability. Battery-current
consumption has not been measured, so no battery-life impact is quantified.

The first request runs approximately 10 seconds after component setup. Subsequent
requests follow `update_interval` (2 minutes by default). `read_now` uses the same path. Requests during startup or an outstanding
read are ignored. Every request sends only `10 5B 01 5C 16` (REQ_UD2, address 1).
There are no configuration writes, retries, test selections, or high-resolution
experimental entities.

## Entities

| Entity key | Type | Unit | Default | Category |
| --- | --- | --- | --- | --- |
| `energy` | Sensor | MWh | Enabled | Normal |
| `volume` | Sensor | m³ | Enabled | Normal |
| `power` | Sensor | kW | Enabled | Normal |
| `flow` | Sensor | m³/h | Enabled | Normal |
| `flow_temperature` | Sensor | °C | Enabled | Normal |
| `return_temperature` | Sensor | °C | Enabled | Normal |
| `temperature_difference` | Sensor | K | Enabled | Normal |
| `error_code` | Sensor | — | Disabled | Diagnostic |
| `battery_operating_duration` | Sensor | d | Disabled | Diagnostic |
| `operating_time_without_error` | Sensor | d | Disabled | Diagnostic |
| `protocol_version` | Sensor | — | Disabled | Diagnostic |
| `meter_datetime` | Text sensor | — | Disabled | Diagnostic |
| `error_start` | Text sensor | — | Disabled | Diagnostic |
| `serial_number` | Text sensor | — | Disabled | Diagnostic |
| `manufacturer` | Text sensor | — | Disabled | Diagnostic |
| `readout_successful` | Binary sensor | — | Enabled | Diagnostic |
| `readout_failures` | Sensor | — | Disabled | Diagnostic |
| `read_now` | Button | — | Enabled | Config |

Energy and volume use `total_increasing`; other main measurements use
`measurement`. Volume uses the `water` device class.
Battery operating duration is **elapsed operating time**, not percentage or
remaining battery life.

Meter error code and error start describe the meter itself. Error start is
`No error` when the error code is zero; otherwise dates use `YYYY-MM-DD HH:MM`,
without timezone conversion. Missing records leave existing states unchanged.

Readout success describes communication: true after a complete checksum-valid
CI `0x72` response with a full variable-data header during a pending request;
false after a 2-second timeout. It stays unknown until the first outcome.
Invalid frames do not end the request early. Failures start at zero on boot,
are never restored, and do not reset on success. The counter uses `total` and
integer display.

## Development

The component lives in `components/qalcosonic_e3/`. Python schemas define the
optional entities; C++ handles non-blocking UART reception, validation, parsing,
and publishing. The parser uses a bounded 261-byte receive buffer and decodes
the meter's normal DIF/VIF records.

Tests cover the captured meter response, decoded values, checksum validation,
malformed frames, incremental reception, and configuration schemas. The fixture
is anonymized: both the header ID and serial number record use `01234567`, and
both the `02 7F` CRC record and M-Bus frame checksum have been recalculated.
The fixture CRC uses the algorithm in section 3.3 of the
[Axioma M-Bus protocol](https://instrumentteam.no/wp-content/uploads/2025/02/QSE3_E4_Mbus_190410.pdf),
with initial value `0xFFFF`, no final XOR, and coverage of the data records
excluding the CRC record, as verified against the original capture. The component
continues to validate the M-Bus frame checksum without enforcing the record CRC.

Run parser and receiver tests with a C++17 compiler (UndefinedBehaviorSanitizer
enabled by default):

```sh
./tests/run.sh
# Optional, on platforms with a working AddressSanitizer runtime:
SANITIZERS=address,undefined ./tests/run.sh
```

Validate and build against ESPHome (development verification uses 2026.9.0):

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt
.venv/bin/python tests/test_config.py
.venv/bin/esphome config tests/esp32-c6.yaml
.venv/bin/esphome compile tests/esp32-c6.yaml
```

The test configuration uses local component sources and dummy Wi-Fi credentials.

## License

[MIT](LICENSE).
