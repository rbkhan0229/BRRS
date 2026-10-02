import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "parallel_halt_trial", Path(__file__).with_name("parallel_halt_trial.py"))
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class HaltOnlySafety(unittest.TestCase):
    def test_both_stops_required(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bundle = root / "bundle"
            bundle.mkdir()
            global_stop = root / "STOP_ALL"
            with self.assertRaises(RuntimeError):
                MODULE.require_stops(bundle, global_stop)
            (bundle / "STOP").touch()
            with self.assertRaises(RuntimeError):
                MODULE.require_stops(bundle, global_stop)
            global_stop.touch()
            MODULE.require_stops(bundle, global_stop)

    def test_worker_command_is_role_and_serial_scoped(self):
        cmd = MODULE.worker_command(Path("/safe/trial.py"), Path("/safe/bundle"),
                                    Path("/safe/STOP_ALL"), "N7", "1050204212")
        self.assertEqual(cmd[2], "worker")
        self.assertEqual(cmd[-4:], ["--role", "N7", "--serial", "1050204212"])
        self.assertNotIn("run", cmd)
        self.assertNotIn("flash", cmd)

    def test_verify_rejects_unhalted_or_low_voltage(self):
        class FakeLink:
            def __init__(self, halted=True, voltage=3300):
                self._halted = halted
                self.hardware_status = type("Status", (), {"VTarget": voltage})()
            def halted(self):
                return self._halted
            def close(self):
                pass
        boards = {"init": {"serial": "0"}, **{f"N{i}": {"serial": str(i)} for i in range(2, 8)}}
        self.assertEqual(len(MODULE.verify_all({"boards": boards}, lambda serial: FakeLink())), 7)
        with self.assertRaises(RuntimeError):
            MODULE.verify_all({"boards": boards}, lambda serial: FakeLink(halted=False))
        with self.assertRaises(RuntimeError):
            MODULE.verify_all({"boards": boards}, lambda serial: FakeLink(voltage=0))


if __name__ == "__main__":
    unittest.main()
