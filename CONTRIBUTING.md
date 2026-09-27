# Contributing

The component lives in `components/qalcosonic_e3/`. Python defines the
configuration schemas; C++ handles UART reception, parsing, and publishing.

## Local development

Nix is **optional**. You can use your locally installed Python (3.14 is used
by CI and the development shell), Git, and a C++17 compiler with sanitizer support
(Clang or GCC) to develop and test this component.

### Setup without Nix

Create a virtual environment and install the test and formatting dependencies:

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-dev.txt -r requirements-format.txt
```

### Optional setup with Nix

If you use Nix, the pinned development shell provides Python 3.14, uv, Clang,
and Git on macOS and Linux:

```sh
nix develop
uv venv --python python3 --seed .venv
uv pip install --python .venv/bin/python -r requirements-dev.txt -r requirements-format.txt
```

The shell uses stable nixpkgs 26.05. `flake.lock` pins the Nix toolchain;
the requirements files pin ESPHome and
pre-commit. When using this setup, run the commands below inside `nix develop`.
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

Validate and build against ESPHome (development verification uses 2026.9.0):

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
