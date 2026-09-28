"""Exercise nested entity defaults and UART validation using real ESPHome."""

import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


class TestConfigLoader(yaml.SafeLoader):
    pass


TEST_SECRETS = yaml.safe_load((ROOT / "tests/secrets.yaml").read_text())
TestConfigLoader.add_constructor("!secret", lambda loader, node: TEST_SECRETS[loader.construct_scalar(node)])
config = yaml.load((ROOT / "tests/esp32-c6.yaml").read_text(), Loader=TestConfigLoader)
config["external_components"][0]["source"]["path"] = str(ROOT / "components")


def validate(value, expected=True):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "device.yaml"
        path.write_text(yaml.safe_dump(value))
        result = subprocess.run(
            [sys.executable, "-m", "esphome", "config", str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        assert (result.returncode == 0) == expected, result.stdout + result.stderr
        return yaml.safe_load(result.stdout) if expected else None


validated = validate(config)["qalcosonic_e3"][0]
assert validated["energy"]["state_class"] == "total_increasing"
assert validated["volume"]["device_class"] == "water"
assert validated["readout_failures"]["state_class"] == "measurement"
assert validated["readout_failures"]["disabled_by_default"] is True
assert validated["readout_successful"]["disabled_by_default"] is False
assert validated["readout_successful"]["device_class"] == "connectivity"
assert validated["unavailable_after_failures"] == 5
config["qalcosonic_e3"] = {"uart_id": "mbus_uart"}
# Every entity is optional, and invalidation is disabled unless configured.
assert validate(config)["qalcosonic_e3"][0]["unavailable_after_failures"] == 0
for threshold in (0, 1, 5, 4294967295):
    config["qalcosonic_e3"]["unavailable_after_failures"] = threshold
    assert validate(config)["qalcosonic_e3"][0]["unavailable_after_failures"] == threshold
for threshold in (-1, 1.5, 4294967296, "invalid"):
    config["qalcosonic_e3"]["unavailable_after_failures"] = threshold
    validate(config, False)
del config["qalcosonic_e3"]["unavailable_after_failures"]
config["uart"]["parity"] = "NONE"
validate(config, False)
config["uart"]["parity"] = "EVEN"
del config["uart"]["tx_pin"]
validate(config, False)
print("ESPHome schema tests passed")
