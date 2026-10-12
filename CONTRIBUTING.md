# Contributing

The component lives in `components/qalcosonic_e3/`. Python defines the
configuration schemas; C++ handles UART reception, parsing, and publishing.

## Code structure and readout flow

- `__init__.py` validates YAML options and connects entities to the C++ component.
- `protocol.h` names shared M-Bus frame bytes, addresses, and frame sizes.
- `link.h` owns link initialization, address discovery, and the frame count bit
  (FCB). After a response, the next request toggles FCB; retries reuse the same
  bit so the meter can repeat its response.
- `parser.cpp` checks frame boundaries and checksum. Its `RecordReader` walks
  records once, skips unknown records, and passes supported records to
  `decode_record()`. The first occurrence of a supported record wins.
- `qalcosonic_e3.cpp` schedules requests, receives UART bytes, handles timeouts,
  and publishes parsed values to ESPHome entities.

After startup, the component moves from `STARTING` to `IDLE`. A read enters
`WAITING_FOR_REPLY` and asks `LinkState` for either SND_NKE (initialize the link)
or REQ_UD2 (request data). A SND_NKE acknowledgement enters `ACK_DELAY`; after
100 ms, the component sends REQ_UD2 and returns to `WAITING_FOR_REPLY`.

A matching response ends the readout in `IDLE`. A data request gets up to three
attempts, each with a two-second timeout. Only exhaustion counts as one failed
readout; a missing initialization acknowledgement ends the readout immediately
after its two-second timeout. Manual discovery uses the same flow but reports
the address without publishing measurements or changing readout diagnostics.

### Address handling and request frames

Auto and manual discovery send SND_NKE and REQ_UD2 to `0xFE`. Discovery accepts
only normal primary response addresses (`0x00`–`0xFA`). Explicit `address: 0xFE`
uses the same response-address validation. Targeted requests accept only the
active primary address; other responses are ignored while waiting until timeout.
After successful discovery, the first targeted read initializes a fresh link
session at the active address.

Short requests use checksum `(control + address) & 0xFF`. SND_NKE uses control
`0x40`; REQ_UD2 alternates `0x7B` / `0x5B`, starting with `0x7B` after link
initialization. Retries preserve the same frame count bit.

`Value<T>::present` distinguishes a missing record from a received zero. Missing
values leave existing entity states unchanged. The parser currently keeps values
decoded before a truncated record or an unsupported variable-length encoding;
header-only frames are also accepted. Keep these semantics in mind when changing
record traversal.

## Local development

Nix is **optional**. You can use your locally installed Python, Git, and a
C++17 compiler with sanitizer support (Clang or GCC) to develop and test this
component. For Python, use the version configured in
[CI](.github/workflows/tests.yml).

### Setup without Nix

Create a virtual environment and install the test and formatting dependencies:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt -r requirements-format.txt
```

### Optional setup with Nix

If you use Nix, the pinned development shell provides Python, uv, Clang,
and Git on macOS and Linux:

```sh
nix develop
uv venv --python python3 --seed .venv
uv pip install --python .venv/bin/python -r requirements-dev.txt -r requirements-format.txt
```

The stable nixpkgs release and tools are configured in [flake.nix](flake.nix),
with the exact revision pinned in `flake.lock`. When using this setup, run the
commands below inside `nix develop`.
To update the Nix toolchain intentionally, run
`nix flake update nixpkgs` and commit the updated lockfile.

### Using the local component

Both setups use the same `.venv/bin/` commands below. Install dependencies
initially and again after requirements change. ESPHome downloads its ESP-IDF
toolchain on the first firmware build.

For a local checkout, replace the Git source in your device configuration with:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [qalcosonic_e3]
```

The path is relative to your device YAML.

## Tests

Run parser and receiver tests with a C++17 compiler (UndefinedBehaviorSanitizer
enabled by default):

```sh
./tests/run.sh
# Optional, on platforms with a working AddressSanitizer runtime:
SANITIZERS=address,undefined ./tests/run.sh
```

Validate and build against the ESPHome version pinned in
[requirements-dev.txt](requirements-dev.txt):

```sh
.venv/bin/python tests/test_config.py
.venv/bin/esphome config tests/esp32-c6.yaml
.venv/bin/esphome compile tests/esp32-c6.yaml
```

The test configuration uses local component sources. The included
[`tests/secrets.yaml`](tests/secrets.yaml) supplies public dummy credentials for
the schema tests, direct ESPHome commands, and CI; no manual secrets setup is
needed. Keep these test values unchanged and use a separate ignored secrets file
for real device credentials.

## Formatting

After either setup above, format tracked files:

```sh
.venv/bin/pre-commit run --all-files
```

The first run downloads the pinned formatters and the Node.js runtime used by
Prettier. Subsequent runs reuse the installed environments. Tool versions are
defined in `.pre-commit-config.yaml`; the runner version is pinned in
`requirements-format.txt`.

Formatting settings are defined in `.clang-format`, `pyproject.toml`,
`.editorconfig`, and `.prettierrc.json`.

If a formatter changes files, pre-commit reports `Failed`. Review the changes
and run the command again; a clean run reports `Passed`. `--all-files` selects
tracked files, including staged additions. To select specific files, use
`--files path/to/file`; individual formatters may also apply ignore rules.

Installing a Git hook is optional. The commands above work without one:

```sh
.venv/bin/pre-commit install
# To remove the optional hook:
.venv/bin/pre-commit uninstall
```

## Pull requests

GitHub Actions runs formatting checks, parser/receiver and schema tests,
configuration validation, and an ESP32-C6 firmware build for pull requests
targeting `main`. Run the relevant checks locally before submitting changes.

To test a pull request on a device, set `ref: refs/pull/NUMBER/head` under the
Git source in `external_components`. Set `refresh: 1min` on the external
component entry while testing remote changes. Compile and flash again to apply
changes to the device.
