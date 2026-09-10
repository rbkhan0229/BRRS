#!/usr/bin/env python3
"""Offline RX-error diagnostic contract tests; never invoke J-Link or firmware builds.

Run: python3 -B -m unittest discover -s Drivers/API/tests -p 'test_exp4_rx_error_diag.py' -v
"""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


API = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("exp4_verify", API / "brrs_exp4_verify.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)


def record(prefix, **values):
    return prefix + "," + ",".join(f"{key}={value}" for key, value in values.items())


def diagnostic_fixture(events=3):
    """Synthetic errors whose pre-rearm bit 12 disappears after rearm."""
    samples = min(events, 64)
    rows = [record("EXP4_RX_ERROR_DIAG_CONFIG_CSV", enabled=1, version=1,
                   status_bytes=6, queue_capacity=32, sample_capacity=64,
                   scope="error_timeout_only", raw_scope="pre_post_48bit",
                   legacy_counter_scope="post_rearm_four_bit",
                   slot_identity="estimate_only", status="PASS"),
            record("EXP4_RX_ERROR_DIAG_CSV", enabled=1, version=1,
                   events=events, errors=events, timeouts=0, processed=events,
                   rearmed=events, queue_overflow=0, sample_capacity=64,
                   samples=samples, samples_omitted=events - samples,
                   pre_zero=0, post_zero=events, changed=events,
                   hw_faults=0, status="PASS"),
            record("EXP4_DEFERRED_CSV", rx_error=events, rx_timeout=0)]
    for bit in range(48):
        rows.append(record("EXP4_RX_ERROR_BIT_CSV", bit=bit, name=f"BIT_{bit}",
                           pre_count=events if bit == 12 else 0, post_count=0))
    for index in range(samples):
        rows.append(record("EXP4_RX_ERROR_SAMPLE_CSV", index=index, event=index + 1,
                           kind="error", sf=index + 1, logical_slot=1,
                           estimated_slot=1, host=index % 2, poll_fint="0x10",
                           post_fint="0x00", pre_lo="0x00001000", pre_hi="0x0000",
                           post_lo="0x00000000", post_hi="0x0000", rearmed=1))
    for phase in ("pre_status", "post_status", "post_fint", "detect_to_rearm_return"):
        rows.append(record("EXP4_RX_ERROR_TIMING_CSV", phase=phase, count=events,
                           min_us=20 if events else 0, max_us=22 if events else 0,
                           avg_x1000_us=21000 if events else 0))
    return rows


class DiagnosticContractTests(unittest.TestCase):
    def check(self, rows, enabled=True):
        verify.verify_rx_error_diag(rows, enabled, expected_cycles=1000)

    def test_valid_empty_small_and_bounded_capture(self):
        for count in (0, 3, 64, 70):
            with self.subTest(events=count):
                self.check(diagnostic_fixture(count))

    def test_old_non_diagnostic_logs_need_no_new_records(self):
        self.check(["EXP4_CONFIG_CSV,rx_path_profile=disabled"], enabled=False)

    def test_valid_timeout_without_rearm_and_high_status_bit(self):
        # Mixed RXERR and timeout records, including RXPREJ (bit33), with
        # the timeout as the final event: there is no following slot to arm.
        rows = diagnostic_fixture(2)
        rows[1] = record("EXP4_RX_ERROR_DIAG_CSV", enabled=1, version=1,
                         events=2, errors=1, timeouts=1, processed=2, rearmed=1,
                         queue_overflow=0, sample_capacity=64, samples=2,
                         samples_omitted=0, pre_zero=0, post_zero=1, changed=1,
                         hw_faults=0, status="PASS")
        rows[2] = record("EXP4_DEFERRED_CSV", rx_error=1, rx_timeout=1)
        for index, row in enumerate(rows):
            values = verify.key_values(row)
            if row.startswith("EXP4_RX_ERROR_BIT_CSV,"):
                bit = int(values["bit"])
                rows[index] = record("EXP4_RX_ERROR_BIT_CSV", bit=bit,
                                     name=f"BIT_{bit}",
                                     pre_count=1 if bit in (12, 17, 33) else 0,
                                     post_count=1 if bit == 17 else 0)
            elif row.startswith("EXP4_RX_ERROR_SAMPLE_CSV,index=0,"):
                rows[index] = row.replace("pre_hi=0x0000", "pre_hi=0x0002")
            elif row.startswith("EXP4_RX_ERROR_SAMPLE_CSV,index=1,"):
                rows[index] = record("EXP4_RX_ERROR_SAMPLE_CSV", index=1,
                                     event=2, kind="timeout", sf=2,
                                     logical_slot=2, estimated_slot=2, host=1,
                                     poll_fint="0x20", post_fint="0x20",
                                     pre_lo="0x00020000", pre_hi="0x0000",
                                     post_lo="0x00020000", post_hi="0x0000",
                                     rearmed=0)
            elif row.startswith("EXP4_RX_ERROR_TIMING_CSV,phase=detect_to_rearm_return,"):
                rows[index] = row.replace("count=2", "count=1")
        self.check(rows)

    def test_semantic_scope_fields_are_required_and_exact(self):
        expected = {"raw_scope": "pre_post_48bit",
                    "legacy_counter_scope": "post_rearm_four_bit",
                    "slot_identity": "estimate_only"}
        for key, value in expected.items():
            for change in ("missing", "incorrect"):
                rows = diagnostic_fixture()
                field = f",{key}={value}"
                rows[0] = rows[0].replace(field, "" if change == "missing" else
                                         f",{key}=incorrect")
                with self.subTest(field=key, change=change), self.assertRaises(verify.VerificationError):
                    self.check(rows)

    def test_off_rejects_unexpected_diagnostic_records(self):
        with self.assertRaises(verify.VerificationError):
            self.check(diagnostic_fixture(), enabled=False)

    def test_on_requires_evidence(self):
        with self.assertRaises(verify.VerificationError):
            self.check([])

    def test_missing_duplicate_and_corrupt_records_fail(self):
        rows = diagnostic_fixture()
        mutations = {
            "missing config": rows[1:],
            "duplicate config": rows + rows[:1],
            "missing bit": [r for r in rows if not r.startswith("EXP4_RX_ERROR_BIT_CSV,bit=47,")],
            "missing sample": [r for r in rows if not r.startswith("EXP4_RX_ERROR_SAMPLE_CSV,index=2,")],
            "missing timing": [r for r in rows if "phase=post_status," not in r],
        }
        for name, broken in mutations.items():
            with self.subTest(name=name), self.assertRaises(verify.VerificationError):
                self.check(broken)

    def test_inconsistent_counters_and_samples_fail(self):
        changes = (
            ("version=1", "version=2"),
            ("queue_overflow=0", "queue_overflow=1"),
            ("hw_faults=0", "hw_faults=1"),
            ("samples_omitted=0", "samples_omitted=1"),
            ("rx_error=3", "rx_error=2"),
            ("pre_count=3", "pre_count=2"),
            ("pre_count=3", "pre_count=-1"),
            ("event=2", "event=1"),
            ("kind=error", "kind=unknown"),
            ("sf=1,", "sf=0,"),
            ("host=0", "host=2"),
            ("pre_hi=0x0000", "pre_hi=0x10000"),
            ("post_lo=0x00000000", "post_lo=0x00001000"),
            ("avg_x1000_us=21000", "avg_x1000_us=25000"),
        )
        for old, new in changes:
            broken = [row.replace(old, new) for row in diagnostic_fixture()]
            with self.subTest(change=(old, new)), self.assertRaises(verify.VerificationError):
                self.check(broken)

    def test_warning_status_cannot_be_hidden_by_zero_summary(self):
        rows = [row.replace("pre_lo=0x00001000", "pre_lo=0x00081000")
                for row in diagnostic_fixture()]
        with self.assertRaises(verify.VerificationError):
            self.check(rows)

    def test_warning_after_sample_capacity_is_still_rejected(self):
        rows = [row.replace("bit=19,name=BIT_19,pre_count=0", "bit=19,name=BIT_19,pre_count=1")
                for row in diagnostic_fixture(70)]
        with self.assertRaises(verify.VerificationError):
            self.check(rows)


class OfflineCliTests(unittest.TestCase):
    def run_script(self, script, args, env=None):
        return subprocess.run(["bash", str(API / script), *args],
                              text=True, capture_output=True, env=env, timeout=10)

    def test_incompatible_modes_rejected_before_any_hardware_access(self):
        commands = {
            "brrs_exp4_build.sh": ["32", "3", "200", "init", "15"],
            "brrs_exp4_capture.sh": ["init", "32", "3", "1", "offline"],
            "brrs_exp4_multi_tx.sh": ["32", "3", "1", "offline"],
        }
        for script, base in commands.items():
            for conflict in ("--irq", "--phy-profile", "--rx-path-profile",
                             "--spim-start-end-profile"):
                with self.subTest(script=script, conflict=conflict):
                    result = self.run_script(script, base + ["--spi-opt", "--rx-error-diag", conflict])
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                    self.assertIn("--rx-error-diag cannot be combined", result.stderr)

    def test_verifier_accepts_flag_but_missing_log_is_failure(self):
        with tempfile.TemporaryDirectory(prefix="exp4-diag-cli-") as tmp:
            base = [sys.executable, "-B", str(API / "brrs_exp4_verify.py"),
                    str(Path(tmp) / "missing.log"), "--role", "init", "--preamble", "32",
                    "--sensors", "3", "--guard", "200", "--lead", "15", "--pac", "8",
                    "--sync-buffer", "2000", "--sync-prep", "2002", "--rx-error-diag"]
            result = subprocess.run(base, text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 3)
            self.assertIn("[verify] FAIL", result.stderr)
            for conflict in ("--irq", "--rx-path-profile", "--spim-start-end-profile"):
                result = subprocess.run(base + [conflict], text=True, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 2)
                self.assertIn("--rx-error-diag cannot be combined", result.stderr)

    def test_build_paths_are_distinct_and_tx_definitions_unchanged(self):
        # Copy just the shell harness into a disposable fake SDK. The mock
        # compiler records arguments and produces inert text, not firmware.
        with tempfile.TemporaryDirectory(prefix="exp4-diag-build-") as tmp:
            root = Path(tmp)
            api = root / "sdk" / "Drivers" / "API"
            api.mkdir(parents=True)
            shutil.copy2(API / "brrs_exp4_build.sh", api / "brrs_exp4_build.sh")
            exe = api / "Build_Platforms/nRF52840-DK/Output/Debug/Exe"
            exe.mkdir(parents=True)
            mock = root / "mock-embuild"
            mock.write_text("#!/usr/bin/env python3\n"
                            "import os, pathlib, sys\n"
                            "out=pathlib.Path(os.environ['MOCK_EXE'])\n"
                            "data='\\n'.join(sys.argv[1:])\n"
                            "(out/'dw3000_api.hex').write_text(data)\n"
                            "(out/'dw3000_api.elf').write_text(data)\n")
            mock.chmod(0o755)
            env = dict(os.environ, EMBUILD=str(mock), MOCK_EXE=str(exe))
            base = ["bash", str(api / "brrs_exp4_build.sh"), "32", "3", "200", "all", "15",
                    "--spi-opt", "--sync-buffer", "2000", "--sync-prep", "2002",
                    "--beacon-preamble", "256"]
            for extra in ([], ["--rx-error-diag"]):
                result = subprocess.run(base + extra, text=True, capture_output=True, env=env, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            normal = exe / "exp4/plen32_sensors3_sb2000_sp2002_sync256_guard200_spiopt"
            diag = normal.with_name(normal.name + "_rxerrdiag")
            self.assertTrue(normal.is_dir())
            self.assertTrue(diag.is_dir())
            for role in ("N2", "N3", "N4"):
                self.assertEqual((normal / f"exp4_32_s3_{role}.hex").read_text(),
                                 (diag / f"exp4_32_s3_{role}.hex").read_text())
                self.assertNotIn("BRRS_OPT_RX_ERROR_DIAG", (diag / f"exp4_32_s3_{role}.hex").read_text())
                self.assertIn("BRRS_SYNC_PREAMBLE_SYMBOLS=256", (diag / f"exp4_32_s3_{role}.hex").read_text())
            self.assertNotIn("BRRS_OPT_RX_ERROR_DIAG", (normal / "exp4_32_s3_init.hex").read_text())
            self.assertIn("BRRS_OPT_RX_ERROR_DIAG=1", (diag / "exp4_32_s3_init.hex").read_text())

    def test_capture_uses_diagnostic_artifact_metadata_and_verifier_flag(self):
        with tempfile.TemporaryDirectory(prefix="exp4-diag-capture-") as tmp:
            root = Path(tmp)
            api = root / "sdk" / "Drivers" / "API"
            api.mkdir(parents=True)
            shutil.copy2(API / "brrs_exp4_capture.sh", api / "brrs_exp4_capture.sh")
            bindir = root / "bin"
            bindir.mkdir()
            for name, body in {
                "arm-nm": "#!/bin/sh\nprintf '20001000 B _SEGGER_RTT\\n'\n",
                "git": "#!/bin/sh\ncase \"$*\" in *rev-parse*) echo MOCK_COMMIT;; *branch*) echo mock-branch;; esac\n",
            }.items():
                target = bindir / name
                target.write_text(body)
                target.chmod(0o755)
            image_dir = api / "Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/plen32_sensors3_sb2000_sp2002_sync256_guard200_spiopt_rxerrdiag"
            image_dir.mkdir(parents=True)
            for suffix in ("hex", "elf"):
                (image_dir / f"exp4_32_s3_init.{suffix}").write_text("INERT MOCK ARTIFACT")
            (api / "rtt_capture.py").write_text(
                "import pathlib,sys\n"
                "pathlib.Path(sys.argv[sys.argv.index('--out')+1]).write_text('MOCK RAW\\n')\n")
            (api / "brrs_exp4_verify.py").write_text(
                "import sys\n"
                "assert '--rx-error-diag' in sys.argv\n"
                "assert '--spi-opt' in sys.argv\n"
                "assert sys.argv[sys.argv.index('--beacon-preamble')+1] == '256'\n"
                "print('[verify] PASS: offline mock only')\n")
            env = dict(os.environ, EMBUILD="/usr/bin/true", ARM_NM=str(bindir / "arm-nm"),
                       PATH=str(bindir) + os.pathsep + os.environ["PATH"])
            cmd = ["bash", str(api / "brrs_exp4_capture.sh"), "init", "32", "3", "1", "offline",
                   "--sync-buffer", "2000", "--sync-prep", "2002", "--spi-opt", "--rx-error-diag",
                   "--beacon-preamble", "256", "--no-build", "--serial", "12345"]
            result = subprocess.run(cmd, text=True, capture_output=True, env=env, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            metas = list((root / "logs").glob("*_spiopt_rxerrdiag/*.meta.txt"))
            self.assertEqual(len(metas), 1)
            metadata = metas[0].read_text()
            self.assertIn("rx_error_diag=enabled-on-init\n", metadata)
            self.assertIn("beacon_preamble_symbols=256\n", metadata)
            self.assertIn("_SPIOPT_RXERRDIAG\n", metadata)
            self.assertIn(str(image_dir / "exp4_32_s3_init.hex"), metadata)
            # Existing diagnostic raw data is protected, just like baseline data.
            result = subprocess.run(cmd, text=True, capture_output=True, env=env, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("refusing to overwrite", result.stderr)

    def test_multi_tx_propagates_flag_without_touching_probes(self):
        with tempfile.TemporaryDirectory(prefix="exp4-diag-multi-") as tmp:
            root = Path(tmp)
            api = root / "sdk" / "Drivers" / "API"
            api.mkdir(parents=True)
            # The app filesystem sandbox can disallow process-substitution
            # /dev/fd redirection. Replace only the console tee in this copy;
            # argument parsing, build/capture forwarding and suffixes remain
            # the real harness. No production script is changed.
            multi = api / "brrs_exp4_multi_tx.sh"
            multi.write_text((API / "brrs_exp4_multi_tx.sh").read_text().replace(
                'exec > >(tee -a "${ORCHESTRATOR_LOG}") 2>&1',
                'exec >>"${ORCHESTRATOR_LOG}" 2>&1'))
            # Even discovery is a stub: this test cannot load pylink.
            (api / "brrs_exp4_probe_assign.py").write_text("print('N2\\t12345')\n")
            build = api / "brrs_exp4_build.sh"
            build.write_text("#!/usr/bin/env python3\n"
                             "import pathlib,sys\n"
                             "assert '--rx-error-diag' in sys.argv\n"
                             "assert sys.argv[sys.argv.index('--beacon-preamble')+1] == '256'\n"
                             "pathlib.Path(__file__).with_name('build.args').write_text(' '.join(sys.argv[1:]))\n")
            build.chmod(0o755)
            capture = api / "brrs_exp4_capture.sh"
            capture.write_text("#!/usr/bin/env python3\n"
                               "import pathlib,sys,time\n"
                               "assert '--rx-error-diag' in sys.argv\n"
                               "assert sys.argv[sys.argv.index('--beacon-preamble')+1] == '256'\n"
                               "pathlib.Path(__file__).with_name('capture.args').write_text(' '.join(sys.argv[1:]))\n"
                               "print('READY marker seen', flush=True)\n"
                               "time.sleep(1.5)\n"
                               "print('[verify] PASS: offline mock only')\n")
            capture.chmod(0o755)
            result = subprocess.run(["bash", str(api / "brrs_exp4_multi_tx.sh"), "32", "1", "1", "offline",
                                     "--rx-error-diag", "--beacon-preamble", "256",
                                     "--probe-serials", "12345"],
                                    text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("--rx-error-diag", (api / "build.args").read_text())
            self.assertIn("--rx-error-diag", (api / "capture.args").read_text())
            self.assertEqual(len(list((root / "logs").glob("*_rxerrdiag/*.assignments.csv"))), 1)
            assignment = next((root / "logs").glob("*_rxerrdiag/*.assignments.csv")).read_text()
            self.assertIn("beacon_preamble_symbols", assignment.splitlines()[0])
            self.assertIn(",256,", assignment.splitlines()[1])


if __name__ == "__main__":
    unittest.main()
