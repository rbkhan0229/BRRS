"""Offline regression tests for optional Exp2 failed-CIR candidate capture."""
import importlib.util
from pathlib import Path
import unittest

API = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "failed_cir_verify", API / "brrs_exp2_failed_cir_verify.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def example():
    lines = [
        "EXP_LOG_CONFIG_CSV,experiment=2,plen=64,sync_plen=512,lead_us=15,tail_us=0,target=1000,cir=1",
        "FAIL_CIR_DUMP_START,version=1,experiment=2,samples=64,max_total=12,max_per_cause=3,scope=candidate_only",
        "FAIL_CIR_HEADER,id=1,cycle=47,node=N2,cause=fce,status=0x00008500,rxprd=1,ciadone=1,accum=43,fp_sample=128,sample_offset=112,n_samples=64",
    ]
    lines += [f"FAIL_CIR_RAW,1,{i},010000FEFFFF" for i in range(64)]
    lines += [
        "FAIL_CIR_DUMP_DONE,version=1,events=3,captured=1,no_preamble=1,invalid_accum=0,no_ciadone=1,cia_error=0,invalid_fp=0,capped=0,samples_per_frame=64",
        "EXP2_DONE,plen=64,expected=1000,rx=997,valid_cir=997,dump_count=997,end_tx=3,collection=PASS,link=LOSS,per_x1000=300,status=PASS",
    ]
    return "\n".join(lines) + "\n"


class FailedCirVerifyTests(unittest.TestCase):
    def test_complete_candidate_and_signed_taps(self):
        report = module.verify(example())
        self.assertEqual(report["captured"], 1)
        self.assertEqual(report["events_with_diagnostics"], 3)
        self.assertEqual(report["cause_counts"], {"fce": 1})
        self.assertEqual(report["frames"][0]["first_tap_iq"], [1, -2])
        self.assertFalse(report["frames"][0]["all_zero"])

    def test_missing_tap_rejected(self):
        text = example().replace("FAIL_CIR_RAW,1,63,010000FEFFFF\n", "")
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(text)

    def test_duplicate_tap_rejected(self):
        text = example().replace("FAIL_CIR_RAW,1,4,010000FEFFFF\n",
                                 "FAIL_CIR_RAW,1,4,010000FEFFFF\n" * 2)
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(text)

    def test_missing_or_early_end_marker_rejected(self):
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("FAIL_CIR_DUMP_DONE", "LOST_DUMP_DONE"))
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("EXP2_DONE,", "EARLY_EXP2_DONE,"))

    def test_gate_and_accounting_mismatch_rejected(self):
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("ciadone=1", "ciadone=0"))
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("events=3", "events=4"))
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("status=0x00008500", "status=0x00008100"))
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("status=0x00008500", "status=0x00000500"))
        with self.assertRaises(module.DiagnosticLogError):
            module.verify(example().replace("status=0x00008500", "status=0x00048500"))

    def test_zero_capture_complete_dump_allowed(self):
        text = "\n".join([
            "EXP_LOG_CONFIG_CSV,experiment=2,plen=32,target=1000,cir=1",
            "FAIL_CIR_DUMP_START,version=1,experiment=2,samples=64,max_total=12,max_per_cause=3,scope=candidate_only",
            "FAIL_CIR_DUMP_DONE,version=1,events=2,captured=0,no_preamble=2,invalid_accum=0,no_ciadone=0,cia_error=0,invalid_fp=0,capped=0,samples_per_frame=64",
            "EXP2_DONE,plen=32,expected=1000,rx=998",
        ])
        self.assertEqual(module.verify(text)["captured"], 0)


if __name__ == "__main__":
    unittest.main()
