"""Offline regression tests for CIR RSSI validity and legacy sentinel handling."""
import math
from pathlib import Path
import sys
import tempfile
import unittest

API = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(API))
import brrs_cir_log_to_csv_plot as cir


class CirRssiValidityTests(unittest.TestCase):
    def test_na_rssi_preserves_fp_snr_and_validates_counts(self):
        lines = [
            "CIR_QUALITY_SCHEMA,version=2,rssi_invalid=NA,fp_snr_independent=1",
            "CIR_CSV,1,1,N2,32,1,1,100,NA,-81.00,NA,100,1,5,1000",
            "CIR_CSV,2,2,N2,32,1,1,100,-79.00,-80.00,1.00,100,1,5,1000",
            "CIR_SUMMARY_CSV,N2,32,2,1000,1000,1000,-7900,-7900,-7900,-8100,-8000,-8050",
            "CIR_VALIDITY_SUMMARY,expected=2,rx=2,records=2,rssi_valid=1,"
            "rssi_invalid=1,fp_valid=2,snr_valid=2,diag_read_errors=0,"
            "rssi_power_zero=1,rssi_sentinel=1,rssi_api_errors=0,collection=PASS",
            "EXP2_DONE,plen=32,expected=2,rx=2,valid_cir=2,dump_count=2,"
            "collection=PASS,link=PASS,per_x1000=0,status=PASS",
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "init.log"
            path.write_text("\n".join(lines) + "\n")
            rows = cir.parse_log(path, "r1", "home", "1.0")
            cir.validate_raw_log(path, rows, 2)
            cir.validate_quality_schema(path, rows)
            raw = cir.summarize(rows, 2)
            firmware = cir.parse_summary_log(path, "r1", "home", "1.0", 2)
            cir.validate_firmware_summary(path, raw, firmware)
            self.assertEqual([row["rssi_valid"] for row in rows], [False, True])
            self.assertTrue(math.isnan(rows[0]["rssi_dbm"]))
            self.assertTrue(math.isnan(rows[0]["rssi_fp_gap_db"]))
            self.assertEqual(rows[0]["fp_dbm"], -81.0)
            self.assertEqual(raw[0]["rssi_valid_n"], 1)
            self.assertEqual(raw[0]["rssi_dbm_mean"], -79.0)
            self.assertEqual(firmware[0]["rssi_dbm_mean"], -79.0)
            path.write_text("\n".join(line.replace("rssi_valid=1", "rssi_valid=2")
                                      for line in lines) + "\n")
            with self.assertRaises(ValueError):
                cir.validate_quality_schema(path, rows)

    def test_all_rssi_invalid_still_produces_fp_snr_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "m32.log"
            path.write_text(
                "CIR_QUALITY_SCHEMA,version=2,rssi_invalid=NA,fp_snr_independent=1\n"
                "CIR_CSV,1,1,N2,32,1,1,100,NA,-81.00,NA,100,1,5,1000\n"
                "CIR_SUMMARY_CSV,N2,32,1,1000,1000,1000,NA,NA,NA,-8100,-8100,-8100\n"
                "CIR_VALIDITY_SUMMARY,expected=1,rx=1,records=1,rssi_valid=0,"
                "rssi_invalid=1,fp_valid=1,snr_valid=1,diag_read_errors=0,"
                "rssi_power_zero=1,rssi_sentinel=1,rssi_api_errors=0,collection=PASS\n")
            rows = cir.parse_log(path, "", "", "")
            cir.validate_quality_schema(path, rows)
            raw = cir.summarize(rows, 1)
            firmware = cir.parse_summary_log(path, "", "", "", 1)
            cir.validate_firmware_summary(path, raw, firmware)
            self.assertEqual(raw[0]["rssi_valid_n"], 0)
            self.assertTrue(math.isnan(raw[0]["rssi_dbm_mean"]))
            self.assertEqual(raw[0]["fp_dbm_mean"], -81.0)

    def test_legacy_q88_sentinel_is_missing_not_minus_128_dbm(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "legacy.log"
            path.write_text(
                "CIR_CSV,1,1,N2,32,1,1,100,-128.00,-81.00,-47.00,100,1,5,1000\n"
                "CIR_SUMMARY_CSV,N2,32,1,1000,1000,1000,-12800,-12800,-12800,-8100,-8100,-8100\n")
            rows = cir.parse_log(path, "", "", "")
            summary = cir.parse_summary_log(path, "", "", "", 1)
            self.assertTrue(math.isnan(rows[0]["rssi_dbm"]))
            self.assertTrue(math.isnan(rows[0]["rssi_fp_gap_db"]))
            self.assertTrue(math.isnan(summary[0]["rssi_dbm_mean"]))
            self.assertEqual(cir.summarize(rows, 1)[0]["rssi_valid_n"], 0)
            self.assertEqual(rows[0]["fp_dbm"], -81.0)


if __name__ == "__main__":
    unittest.main()
