"""Validate the delivered YAML and reject unsupported or conflicting settings."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]


class ConfigurationTest(unittest.TestCase):
    def validate(self, transform=lambda source: source):
        with tempfile.TemporaryDirectory(prefix="roger-config-") as directory:
            directory = Path(directory)
            (directory / "components").symlink_to(ROOT / "components", target_is_directory=True)
            (directory / "secrets.yaml").write_text(
                "wifi_ssid: TEST_ONLY\n"
                "wifi_password: test-only-password\n"
                "ota_password: test-only-ota\n"
                "api_encryption_key: AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n"
            )
            config = directory / "test-cancello.yaml"
            source = (ROOT / "examples" / "esp32-gate.yaml").read_text()
            # Test the exact checkout under review, without requiring published files.
            source = source.replace(
                "  - source: github://branda92/esphome-roger-edge1@main",
                "  - source:\n      type: local\n      path: components",
            )
            config.write_text(transform(source))
            return subprocess.run(
                [sys.executable, "-m", "esphome", "config", str(config)],
                capture_output=True, text=True, timeout=60,
            )

    def test_delivered_configuration(self):
        result = self.validate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_parameter_38(self):
        result = self.validate(lambda s: s + '\n  - platform: roger_edge1\n    name: "Parametro 38"\n    parameter: 38\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_settings(self):
        cases = [
            ("    parameter: 80", "    parameter: 99", "38"),
            ("    command: OPEN", "    command: TOGGLE", "OPEN"),
            ("  address: 0x0A", "  address: 0", "at least 1"),
            ("  offline_timeout: 3s", "  offline_timeout: 500ms", "offline_timeout must exceed"),
            ("  response_timeout: 200ms", "  response_timeout: 0ms", "50ms"),
            ("  parameter_update_interval: 30s", "  parameter_update_interval: 500ms", "1s"),
            ("  parameter_update_interval: 30s", "  parameter_update_interval: 25h", "24h"),
            ("  inputs_update_interval: 500ms", "  inputs_update_interval: 50ms", "100ms"),
            ("  inputs_update_interval: 500ms", "  inputs_update_interval: 11s", "10s"),
            ("  inputs_timeout: 3s", "  inputs_timeout: 500ms", "inputs_timeout must exceed"),
            ("  inputs_timeout: 3s", "  inputs_timeout: 61s", "60s"),
            ("  rx_pin: GPIO16\n", "", "rx_pin"),
            ("  parity: NONE", "  parity: EVEN", "parity NONE"),
            ("  baud_rate: 115200", "  baud_rate: 9600", "baud rate 115200"),
        ]
        for old, new, message in cases:
            with self.subTest(setting=old):
                result = self.validate(lambda s: s.replace(old, new))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stdout + result.stderr)

    def test_two_consumers_on_same_uart_rejected(self):
        result = self.validate(lambda s: s + '\nmodbus:\n  uart_id: uart_roger\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("can only be used by one", result.stdout + result.stderr)

    def replace_binary_entities(self, source, body):
        before, rest = source.split('\nbinary_sensor:\n', 1)
        _, after = rest.split('\nselect:\n', 1)
        return (before + '\nbinary_sensor:\n  - platform: roger_edge1\n'
                '    roger_edge1_id: edge1\n' + body + '\nselect:\n' + after)

    def test_optional_binary_entities(self):
        for body in ['    online:\n      name: Online\n',
                     '    ft1:\n      name: FT1\n',
                     '    ft2:\n      name: FT2\n',
                     '    ft1:\n      name: FT1\n    ft2:\n      name: FT2\n']:
            with self.subTest(body=body):
                result = self.validate(lambda s: self.replace_binary_entities(s, body))
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_binary_platform_rejected(self):
        result = self.validate(lambda s: self.replace_binary_entities(s, ''))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ft1', result.stdout + result.stderr)

    def test_configuration_without_input_features(self):
        def legacy(source):
            source = self.replace_binary_entities(source, '    online:\n      name: Online\n')
            source = re.sub(r'^  inputs_(update_interval|timeout):.*\n', '', source, flags=re.M)
            source = re.sub(r'^    (inputs_raw|input_aux_raw):\n(?:      .*\n)+', '', source, flags=re.M)
            return source
        result = self.validate(legacy)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
