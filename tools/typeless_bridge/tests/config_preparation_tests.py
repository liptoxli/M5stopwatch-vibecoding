import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("prepare_cloud_sync", Path(__file__).resolve().parents[1] / "prepare_cloud_sync.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ConfigurationTests(unittest.TestCase):
    def test_private_and_distinct_without_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "private"
            module.prepare("https://sync.example.com/api/rlcd/codex", output, 2, "synthetic-test-account")
            files = sorted(output.glob("*.json"))
            first, second = [json.loads(file.read_text()) for file in files]
            self.assertEqual(first["account_scope"], second["account_scope"])
            self.assertEqual(first["token"], second["token"])
            self.assertNotEqual(first["device_id"], second["device_id"])
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o700)
            for file in output.iterdir():
                self.assertEqual(stat.S_IMODE(file.stat().st_mode), 0o600)
                self.assertNotIn("synthetic-test-account", file.read_text())
            before = {p.name: p.read_bytes() for p in output.iterdir()}
            with self.assertRaises(FileExistsError):
                module.prepare(first["endpoint"], output, 2, "synthetic-test-account")
            self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})

    def test_reject_unsafe_endpoint_or_repository_output(self):
        with tempfile.TemporaryDirectory() as temp:
            for url in ("http://example.com", "https://user:secret@example.com", "https://example.com?token=secret"):
                with self.assertRaises(ValueError):
                    module.prepare(url, Path(temp) / "new", 2, "test")
            with self.assertRaises(ValueError):
                module.prepare("https://sync.example.com", Path(module.__file__).resolve().parent / "private", 2, "test")


if __name__ == "__main__":
    unittest.main()
