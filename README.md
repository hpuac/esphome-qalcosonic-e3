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
  transport: optical
  address: 0x01
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
  automatic_readout:
    name: "Automatic Readout"
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
  transport: optical
  address: 0x01
  error_code:
    name: "Error Code"
    disabled_by_default: false
```

The component requires both UART pins and validates 2400 baud, 8 data bits,
even parity, and 1 stop bit. Use a dedicated UART with a 512-byte RX buffer.

## M-Bus primary address

`transport` is **required** and accepts exactly `optical` or `wired`. Both use
one shared parser. `address` is also **required**, with no default, and accepts
`auto` or a supported numeric address. Configure the primary address for
the interface you use: optical and wired interfaces may have different primary
addresses even on the same physical meter. On the tested meter, optical used
`0x01` and wired M-Bus used `0x2C` (decimal 44).

Supported normal primary addresses are `0x00`–`0xFA`; `0xFE` is also supported as
a **test/point-to-point address**. Hexadecimal notation is supported.
`0xFB`, `0xFC`, `0xFD`, and `0xFF` are not supported. Secondary addressing/selection
with `0xFD` is not implemented. Broadcast address `0xFF` expects no response and
is incompatible with this component's request/response behavior.

Optical example (`0x01` was the tested meter's optical address):

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: optical
  address: 0x01
  update_interval: 2min
```

Recommended wired point-to-point setup, with **exactly one connected slave**:

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: wired
  address: auto
  update_interval: 15min
```

Auto mode sends SND_NKE and REQ_UD2 to `0xFE`, logs the discovered primary
address, and uses that address for following normal reads. The first targeted
read initializes a fresh link session at the discovered address. The discovered
address is kept in RAM only; discovery runs again after reboot. A failed initial
discovery retries `0xFE`, without assuming an address such as `0x01`.
Auto discovery is valid for both optical and wired transports. It has been
verified on wired M-Bus so far and is useful for single-slave setups.

Known wired address (`0x2C`, decimal 44, was the same meter's wired address):

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: wired
  address: 0x2C
  update_interval: 15min
```

Discovery/testing with **exactly one connected M-Bus slave**:

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: wired
  address: 0xFE
```

Both `address: auto` and manual discovery are safe only with **exactly one
connected M-Bus slave**. Every connected slave recognizes the test/point-to-point address `0xFE`, so use
it only with **exactly one slave connected**. Slaves respond using their real
primary address, which is logged for discovery. It is not appropriate for normal
multi-slave bus operation; this component does not implement a bus scanner.
All requests to `0xFE`, including auto and manual discovery, accept only normal
primary response addresses `0x00`–`0xFA`; responses with `0xFB`–`0xFF` are ignored.
For normal targeted requests, responses from addresses other than the active
address are ignored; the component keeps waiting for a matching response until timeout.
On the tested wired interface, `10 40 FE 3E 16` (SND_NKE) received `E5`, and
`10 5B FE 59 16` (REQ_UD2) received a valid frame with response address `0x2C`.
Requests are generated from the active request address, with checksum
`(control + address) & 0xFF`. REQ_UD2 retains the existing alternating frame count
bit (`0x7B` / `0x5B`) and retry behavior.

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
flash or publish measurements. A failed manual discovery keeps the previous
active address; unresolved auto mode continues discovery on the next read.
Presses during startup or an active read are ignored with a warning. The button
also works when automatic readout is disabled. Use it only with one slave.

## Optical wiring

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

## Wired M-Bus hardware

The tested **Zihatec M-BUS Master Module** worked with an ESP32-C6 and a single
QALCOSONIC E3. See the [product/project page](https://www.hwhardsoft.de/2024/10/08/m-bus-master-breakout/).
The exact small module tested has three connector groups:

| Group                     | Connector labels                        |
| ------------------------- | --------------------------------------- |
| Power / M-Bus-side supply | `MGND`, `5-24V`                         |
| M-Bus                     | `MBUS+`, `MBUS-`                        |
| Digital side              | `3.3-5VDC`, `MBUS RX`, `MBUS TX`, `GND` |

Tested digital wiring:

| ESP32        | Zihatec module |
| ------------ | -------------- |
| 3.3 V        | `3.3-5VDC`     |
| GND          | Digital `GND`  |
| RX (GPIO2) ← | `MBUS RX`      |
| TX (GPIO3) → | `MBUS TX`      |

**For this module, RX connects to RX and TX connects to TX.**
`ESP RX GPIO2 ← Zihatec MBUS RX`; `ESP TX GPIO3 → Zihatec MBUS TX`.
`MBUS RX` is the isolated data **output** from the module toward the controller;
`MBUS TX` is the isolated data **input** from the controller.
Do not cross these signals as you might with conventional UART labels.

Connect the meter's M-Bus pair to `MBUS+` / `MBUS-`. The tested meter's M-Bus
connection is non-polarized, so either meter M-Bus wire can go to either terminal.
**Do not connect the meter's M-Bus wires directly to ESP GPIO pins.** A proper
M-Bus master/transceiver is required.

### Recommended wiring: Shared ESP supply, no galvanic isolation

This wiring uses the **exact Zihatec M-BUS Master Module linked above**:

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

`ESP RX GPIO2 ← Zihatec MBUS RX`; `ESP TX GPIO3 → Zihatec MBUS TX`.
For this specific module **RX connects to RX and TX connects to TX**.
The tested meter's pair is non-polarized: either wire can connect to `MBUS+` or
`MBUS-`. Never connect those wires directly to ESP GPIO.

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

## Optical interface and polling

On the tested QALCOSONIC E3, the optical interface became inactive after approximately **5 minutes without
communication**. After it sleeps, it requires local activation at the meter before communication resumes.
The **2-minute** polling interval was used successfully for a long period. It
was chosen deliberately to leave margin for failed reads while keeping the
interface available. Use an
interval below the inactivity timeout for continuous availability. Battery-current
consumption has not been measured, so no battery-life impact is quantified.
The optical interface is normally allowed to deactivate after inactivity;
continuous polling below that timeout keeps it active.

The optical interface was used successfully during development, but on the
tested installation it later became unreliable despite previously working.
No root cause was established, and this does not imply that all QALCOSONIC E3
optical interfaces will fail. For permanent installations, wired M-Bus is
therefore recommended when accessible.

With `transport: optical`, about five minutes without link activity resets the
link so the next read starts with SND_NKE. With `transport: wired`, no optical-style
five-minute timeout is applied: polling intervals such as 15 minutes or one hour
do not force link resets. Choose the wired polling interval for your use case;
the global default remains two minutes. 15 minutes is used as an example for
wired installations. Choose the interval according to your use case; shorter
and longer intervals are supported without an optical-style inactivity constraint.
Both transports start the first read after boot with SND_NKE and resynchronize
after failed communication. Alternating FCB and retry behavior are preserved.

With automatic readout enabled, the first reading starts about 10 seconds after
startup. Use `update_interval` to change the polling interval. Configure
`automatic_readout` to expose a switch that pauses scheduled reads when off.
The switch starts on after a reboot by default. Turning it back on restarts
the polling interval. `read_now` still requests a reading while automatic
readout is off. A read already in progress finishes when the switch is turned off.
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
| `automatic_readout`            | Switch        | —    | Enabled  | Config     |
| `discover_address`             | Button        | —    | Enabled  | Config     |
| `read_now`                     | Button        | —    | Enabled  | Config     |

Battery operating duration is **elapsed operating time**, not percentage or
remaining battery life.

Meter error code and error start describe the meter itself. Error start is
`No error` when the error code is zero; otherwise dates use `YYYY-MM-DD HH:MM`,
without timezone conversion. Missing records leave existing states unchanged.

`readout_successful` reports whether the last read succeeded and stays unknown
until the first result. A read times out after 2 seconds. `readout_failures`
counts consecutive failed reads since the last successful read and resets to
zero after a successful read or when the ESP restarts.

`unavailable_after_failures` sets the number of consecutive failed reads before
numeric meter sensors publish `NaN` (shown as **unknown** in Home Assistant).
They publish `NaN` once per failure streak, when the threshold is reached.
The default `0` keeps the last measurements.

```yaml
qalcosonic_e3:
  uart_id: mbus_uart
  transport: optical
  address: 0x01
  update_interval: 2min
  unavailable_after_failures: 5
```

A successful read resets the failure streak and updates received values.
Text sensors retain their values; sensor filters can suppress or replace `NaN`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, tests, and formatting.

## License

[MIT](LICENSE).
