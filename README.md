# ESPHome QALCOSONIC E3

ESPHome external component for reading Axioma QALCOSONIC E3 heat meters via M-Bus over UART.

Read energy, volume, power, flow, and temperatures directly into Home Assistant,
with meter diagnostics and a manual read button. The component supports optical
M-Bus through a TTL optical read/write head and wired M-Bus through a proper
M-Bus master/transceiver. Both use the same protocol and parser.

**Wired M-Bus is recommended when you have access to the wired M-Bus connection.**
It avoids optical alignment/head-placement problems and the optical inactivity
timeout, making it better suited to permanent unattended installations.

## Hardware

Both tested setups use an **Axioma QALCOSONIC E3** and a
[**Waveshare ESP32-C6-Zero**](https://www.amazon.de/dp/B0F12PRH9G) with 8 MB flash:

- **Optical:** a [TTL IR read/write head](https://www.ebay.de/itm/356650595031)
  powered from 3.3 V. See [optical wiring](#optical-wiring).
- **Wired:** a [Zihatec M-BUS Master Module](https://www.hwhardsoft.de/2024/10/08/m-bus-master-breakout/)
  with 3.3 V digital logic and its M-Bus side powered from the board's 5 V rail
  or a separate supply. See [wired M-Bus hardware and wiring](#wired-m-bus-hardware).

Other ESPHome-supported boards and compatible interfaces should work with the
appropriate board configuration and wiring. The UART interface must support
both sending and receiving, with logic levels compatible with the board.
Both transports use **2400 baud, 8E1**; wired M-Bus requires a proper master/transceiver.
The decoded data is specific to the QALCOSONIC E3.

## Installation and configuration

The following configuration enables the main measurements, readout
status, automatic readout switch, and manual read button. Choose `optical` or
`wired` for your hardware, and set its primary address or use `auto` to discover
it with exactly one connected meter:

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
  transport: optical # Use wired for an M-Bus master/transceiver.
  address: 0x01 # Your interface's primary address, or auto (one connected meter).
  update_interval: 2min # Optical: keep below the inactivity timeout; wired: e.g. 15min.
  energy:
    name: "Energy"
  flow:
    name: "Flow"
  flow_temperature:
    name: "Flow Temperature"
  power:
    name: "Power"
  return_temperature:
    name: "Return Temperature"
  temperature_difference:
    name: "Temperature Difference"
  volume:
    name: "Volume"
  readout_successful:
    name: "Readout Successful"
  automatic_readout:
    name: "Automatic Readout"
  read_now:
    name: "Read Now"
```

Add your Wi-Fi credentials and API encryption key to `secrets.yaml`, and adjust
the board and GPIO pins for your hardware.

[The full example](examples/esp32-c6.yaml) includes every optional entity.
Each nested entity is optional and supports normal ESPHome entity options,
such as `id`, numeric sensor filters, and `disabled_by_default`. Omitted entities
are not created. Configured diagnostic entities are disabled in Home Assistant
by default, except for `readout_successful`. Enable them in Home Assistant or set
`disabled_by_default: false` on the entity.

The component requires both UART pins and validates **2400 baud, 8E1**.
Use a dedicated UART; a 512-byte RX buffer is recommended.

### Component options

| Option                       | Required | Default | Values / behavior                                                                                 |
| ---------------------------- | -------- | ------- | ------------------------------------------------------------------------------------------------- |
| `transport`                  | Yes      | —       | `optical` or `wired`                                                                              |
| `address`                    | Yes      | —       | `auto`, `0x00`–`0xFA`, or `0xFE`; see below                                                       |
| `update_interval`            | No       | `2min`  | Polling interval for either transport                                                             |
| `unavailable_after_failures` | No       | `0`     | Consecutive failed reads before numeric meter sensors become unknown; `0` keeps their last values |

## M-Bus primary address

Configure the primary address for the interface you use: optical and wired
interfaces may have different addresses even on the same meter. On the tested
meter, optical used `0x01` and wired M-Bus used `0x2C` (decimal 44).

Supported numeric addresses are `0x00`–`0xFA` and the test/point-to-point address
`0xFE`. Hexadecimal notation is supported. Secondary-address selection and
multi-slave scanning are not implemented; `0xFB`–`0xFD` and `0xFF` are rejected.

**Auto discovery, manual discovery, and explicit `0xFE` operation require exactly
one connected M-Bus slave.** With multiple slaves, responses may collide.

Recommended wired point-to-point setup:

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: wired
  address: auto
  update_interval: 15min
```

- `address: auto` discovers the primary address via `0xFE`, logs it, and uses it
  for subsequent reads. It is kept in RAM only; discovery runs again after
  reboot. Failed initial discovery is retried on the next read. Auto discovery
  supports both transports and has been verified on wired M-Bus so far.
- A known numeric address, such as `address: 0x2C`, is used directly.
- `address: 0xFE` keeps using the test/point-to-point address for every read and
  logs the meter's primary response address.

### Manual address discovery

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: wired
  address: auto
  update_interval: 15min
  discover_address:
    name: "Discover M-Bus Address"
```

The optional button starts discovery via `0xFE` and logs the result. In auto mode
it updates the runtime address; with a numeric address it only reports the
result and keeps using the configured address. Discovery does not persist to
flash, publish measurements, or change readout diagnostics and the failure count.
A failed manual discovery keeps the previous active address; unresolved auto mode continues discovery on the next read.
Presses during startup or an active read are ignored with a warning. The button
also works when automatic readout is disabled.

## Optical wiring

| ESP32      | TTL optical head |
| ---------- | ---------------- |
| 3.3 V      | VCC              |
| GND        | GND              |
| GPIO2 (RX) | TX → ESP RX      |
| GPIO3 (TX) | RX ← ESP TX      |

Pins are configurable. The optical head must support M-Bus, **not SML**.

## Wired M-Bus hardware

The tested **Zihatec M-BUS Master Module** worked with an ESP32-C6 and a single
QALCOSONIC E3. See the [product/project page](https://www.hwhardsoft.de/2024/10/08/m-bus-master-breakout/).

**For this module, RX connects to RX and TX connects to TX.** `MBUS RX` is the
module's data output toward ESP RX; `MBUS TX` is its data input from ESP TX.
Do not cross these signals as you might with conventional UART labels.

Connect the meter's pair to `MBUS+` / `MBUS-`. The tested meter's M-Bus connection
is non-polarized, so either wire can go to either terminal.
**Never connect the meter's M-Bus wires directly to ESP GPIO pins.**

### Recommended wiring: Shared ESP supply, no galvanic isolation

```text
ESP32-C6                    Zihatec M-BUS Master             QALCOSONIC E3
─────────                   ────────────────────             ─────────────
3.3V   ───────────────────> 3.3-5VDC
GND    ───────────────────> GND
GPIO2  <─────────────────── MBUS RX
GPIO3  ───────────────────> MBUS TX
5V     ───────────────────> 5-24V
GND    ───────────────────> MGND
                            MBUS+ ────────────────────────── M-Bus
                            MBUS- ────────────────────────── M-Bus
```

This exact setup was tested successfully: the development ESP32-C6 board's
nominal 5 V rail powered the M-Bus side for a single meter. No separate external
supply is required for that tested setup. Digital GND and MGND are common through
the ESP supply, so **galvanic isolation is lost**. Other ESP boards may have
weaker 5 V rails; sufficient voltage/current is not universally guaranteed.

If isolation is desired or the board's 5 V rail is insufficient, a separate
suitable 5–24 V supply is an optional alternative. A 9 V external supply was
successfully tested and produced approximately 36 V across `MBUS+` / `MBUS-`
with no bus fault. Follow the module's voltage/current limits and use a suitably
rated, current-limited supply; 9–12 V is a reasonable choice.

```text
ESP32 digital wiring: same 3.3V, GND, GPIO2 and GPIO3 connections as above
External +9V (or suitable 5–24V) ───> Zihatec 5-24V
External supply GND             ───> Zihatec MGND
Meter M-Bus pair: same MBUS+ / MBUS- connections as above
```

To preserve galvanic isolation, **do not connect MGND or external supply GND to
ESP digital GND**. In this alternative, the ESP 5 V rail is not connected to the
module's `5-24V` input.

## Polling and readout control

On the tested QALCOSONIC E3, the optical interface became inactive after about
**5 minutes without communication** and required local activation at the meter
to resume. A **2-minute** interval worked successfully for a long period and
leaves margin for failed reads. Keep the interval below the inactivity timeout
for continuous optical availability. Battery-current consumption has not been
measured, so the impact on battery life is unknown.

The tested optical installation later became unreliable; no root cause was
established. This observation does not establish a general failure of E3 optical
interfaces.

With `transport: optical`, five minutes without link activity causes the next
read to reinitialize the link with SND_NKE. Wired mode has no such inactivity
reset; choose its polling interval for your use case. The default is **2 minutes
for both transports**; 15 minutes is only the wired example's choice.
Both transports initialize the link on their first read and resynchronize after
failed communication.

With automatic readout enabled, the first reading starts about 10 seconds after
startup. Use `update_interval` to change the polling interval. Configure
`automatic_readout` to expose a switch that pauses scheduled reads when off.
The switch starts on after a reboot by default. Turning it back on restarts
the polling interval. `read_now` still requests a reading while automatic
readout is off. Requests during startup or an active read are ignored rather
than queued. A read already in progress finishes when the switch is turned off.
The component only reads meter data; it does not change meter settings.

## Entities

| Entity key                     | Type          | Unit | Enabled by default | Category   |
| ------------------------------ | ------------- | ---- | ------------------ | ---------- |
| `energy`                       | Sensor        | MWh  | Enabled            | Normal     |
| `flow`                         | Sensor        | m³/h | Enabled            | Normal     |
| `flow_temperature`             | Sensor        | °C   | Enabled            | Normal     |
| `power`                        | Sensor        | kW   | Enabled            | Normal     |
| `return_temperature`           | Sensor        | °C   | Enabled            | Normal     |
| `temperature_difference`       | Sensor        | K    | Enabled            | Normal     |
| `volume`                       | Sensor        | m³   | Enabled            | Normal     |
| `battery_operating_duration`   | Sensor        | d    | Disabled           | Diagnostic |
| `error_code`                   | Text sensor   | —    | Disabled           | Diagnostic |
| `error_start`                  | Text sensor   | —    | Disabled           | Diagnostic |
| `manufacturer`                 | Text sensor   | —    | Disabled           | Diagnostic |
| `meter_datetime`               | Text sensor   | —    | Disabled           | Diagnostic |
| `operating_time_without_error` | Sensor        | d    | Disabled           | Diagnostic |
| `protocol_version`             | Sensor        | —    | Disabled           | Diagnostic |
| `readout_failures`             | Sensor        | —    | Disabled           | Diagnostic |
| `readout_successful`           | Binary sensor | —    | Enabled            | Diagnostic |
| `serial_number`                | Text sensor   | —    | Disabled           | Diagnostic |
| `automatic_readout`            | Switch        | —    | Enabled            | Config     |
| `discover_address`             | Button        | —    | Enabled            | Config     |
| `read_now`                     | Button        | —    | Enabled            | Config     |

Battery operating duration is **elapsed operating time**, not percentage or
remaining battery life.

The E3 error field is a bit/status field. `error_code` exposes the meter-style
LCD code as text: `0000` for no LCD error, or codes such as `4000`, `00C0`,
`0C00`, and `0880`. Leading zeroes are preserved. The full raw status is retained
internally; flags without an LCD indication do not contribute to the text code.

Meter error code and error start describe the meter itself. Error start is
`No error` when the raw error status is zero; otherwise dates use `YYYY-MM-DD HH:MM`,
without timezone conversion. Missing records leave existing states unchanged.

`readout_successful` reports whether the last read succeeded and stays unknown
until the first result. Each data request has a **2-second timeout** and is
attempted up to **three times**. Only exhaustion counts as one failed readout;
a missing link-initialization acknowledgement ends the readout after 2 seconds.
`readout_failures` counts consecutive failed readouts and resets to zero after a
successful read or when the ESP restarts.

`unavailable_after_failures` sets the number of consecutive failed reads before
numeric meter sensors publish `NaN` (shown as **unknown** in Home Assistant).
They publish `NaN` once per failure streak, when the threshold is reached.
The default `0` keeps the last measurements.

A successful read resets the failure streak and updates received values.
Text sensors retain their values; sensor filters can suppress or replace `NaN`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, tests, and formatting.

## License

[MIT](LICENSE).
