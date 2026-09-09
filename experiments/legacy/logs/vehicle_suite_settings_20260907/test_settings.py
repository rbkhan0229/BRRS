"""Meaningful negative and historical-log checks; no J-Link/RF calls."""
from pathlib import Path
import copy, importlib.util, json, os, subprocess, sys, tempfile, unittest

BASE = Path('/Users/songchieon/Desktop/DWM3000')
ROOT = Path(__file__).resolve().parent
API = BASE / 'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0, str(API))
import brrs_suite_manifest as manifest
import brrs_exp4_verify as verify
import rtt_capture

class SettingsTests(unittest.TestCase):
    def setUp(self):
        self.m = manifest.load(API / 'brrs_vehicle_manifest.json')

    def frozen(self):
        m = copy.deepcopy(self.m)
        m['lead_selection'] = {'frozen': True, 'lead_us_by_pac': {'4': 17, '8': 25}, 'evidence': 'TEST FIXTURE ONLY, NOT A FIELD SELECTION'}
        return m

    def test_fixed_board_identity_even_when_usb_order_changes(self):
        actual = [self.m['boards'][r]['serial'] for r in reversed(manifest.ROLES[1:])]
        got = dict(manifest.fixed_assignments(self.m, 6, actual))
        self.assertEqual(got['N4'], '1050282818')
        self.assertEqual(got['N6'], '1050227627')
        self.assertEqual(manifest.serial_for(self.m, 'exp1', 'tx'), '1050282818')
        with self.assertRaises(ValueError): manifest.fixed_assignments(self.m, 6, actual[:-1])
        with self.assertRaises(ValueError): manifest.fixed_assignments(self.m, 6, actual[:-1] + ['999999999'])

    def test_unselected_lead_blocks_main_stages(self):
        for stage in ['exp1','exp2','exp3','exp4','exp5']:
            with self.subTest(stage=stage), self.assertRaises(ValueError): manifest.plan(self.m,stage)
        cases = manifest.plan(self.m,'stage0')
        self.assertEqual(len(cases),82)
        self.assertEqual({c['conditions']['rx_pac'] for c in cases}, {4,8})

    def test_pac_specific_leads_and_single_link_mapping(self):
        for stage in ['exp1','exp2']:
            cases = manifest.plan(self.frozen(),stage)
            self.assertEqual(len(cases),8)
            for c in cases:
                p = c['conditions'];self.assertEqual(p['lead_us'],{4:17,8:25}[p['rx_pac']])
                tx = next(j for j in c['jobs'] if j['logical_node']==2)
                self.assertEqual((tx['physical_role'],tx['serial']),('N4','1050282818'))
                self.assertEqual(len(c['inactive_tx_roles']),5)
                for j in c['jobs']:
                    self.assertEqual(j['argv'][j['argv'].index('--pac')+1],str(p['rx_pac']))
                    self.assertEqual(j['argv'][j['argv'].index('--lead')+1],str(p['lead_us']))

    def test_exp4_node_count_differs_from_slots_and_keeps_rx_mode(self):
        cases = manifest.plan(self.frozen(),'exp4')
        self.assertEqual(len(cases),12)
        self.assertEqual({m:manifest.max_slots(self.m['exp4'],m) for m in [32,64,256]}, {32:13,64:12,256:8})
        for c in cases:
            self.assertEqual(len(c['jobs']),7)
            p=c['conditions'];self.assertEqual(p['rx_window_us'],manifest.AIRTIME_US[p['preamble']]+p['lead_us'])
            if p['preamble']==32 and len(p['slot_owners'])==13:self.assertEqual(p['slot_owners'],'2345672345673')
            for j in c['jobs']:
                self.assertIn('--slotted-rx',j['argv']);self.assertIn('--spi-opt',j['argv'])

    def test_exp3_and_exp5_preserve_their_phy(self):
        self.assertEqual([c['conditions']['variant'] for c in manifest.plan(self.frozen(),'exp3')],['A','B','C'])
        p=manifest.plan(self.frozen(),'exp5')[0]['conditions']
        self.assertEqual((p['preamble'],p['rx_pac'],p['lead_us']),(1024,32,25))

    def test_invalid_duplicate_serial_and_excess_slots_rejected(self):
        for kind in ['serial','slots']:
            m=self.frozen()
            if kind=='serial':m['boards']['N6']['serial']=m['boards']['N4']['serial']
            else:m['exp4']['slot_counts_by_preamble']['256']=[13]
            with tempfile.NamedTemporaryFile(mode='w',suffix='.json') as f:
                json.dump(m,f);f.flush()
                with self.assertRaises(ValueError):manifest.load(f.name)

    def test_historical_slotted_loss_is_preserved(self):
        raw=BASE/'logs/exp4_home_s6_multislot_pac_ab_20260907/R13P4_r1/init/init.log'
        lines=raw.read_text().splitlines()
        def inspect(x, **kw):
            return verify.verify_init(x,32,6,250,25,4,3000,2500,100,'2345672345673',spi_opt=True,slotted_rx=True,**kw)
        text=inspect(lines)
        self.assertIn('per_node_goal=FAIL_PER',text);self.assertIn('worst_node_per=7.150%',text)
        for old,new in [('window_us=122','window_us=112'),('fwto_uus=119','fwto_uus=110'),('rdb_resync=0','rdb_resync=1')]:
            with self.subTest(field=old),self.assertRaises(verify.VerificationError):inspect([l.replace(old,new) for l in lines])
        with self.assertRaises(verify.VerificationError):inspect(lines+[next(l for l in lines if l.startswith('EXP4_SLOT_RX_CSV,'))])

    def test_ready_end_missing_or_duplicate_rejected(self):
        good='EXP_LOG_READY,channel=1\ndata\nEXP2_DONE,rx=1\n'
        self.assertEqual(rtt_capture.marker_errors(good,'EXP_LOG_READY,channel=1','EXP2_DONE,'),[])
        for text in [good.replace('EXP_LOG_READY,channel=1',''),good+'EXP2_DONE,rx=1\n',good+'EXP_LOG_READY,channel=1\n']:
            self.assertTrue(rtt_capture.marker_errors(text,'EXP_LOG_READY,channel=1','EXP2_DONE,'))

    def test_runner_forwards_board_and_exp4_options(self):
        cmd=['bash',str(API/'brrs_run_experiment.sh'),'exp4','N4','settings_test','--sensors','6','--preambles','32','--guard','250','--lead','25','--pac','8','--slotted-rx','--spi-opt','--board-map',str(API/'brrs_vehicle_manifest.json'),'--dry-run']
        r=subprocess.run(cmd,capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr)
        command=next(l for l in r.stdout.splitlines() if l.startswith('  '))
        for part in ['--serial 1050282818','--slotted-rx','--spi-opt','--sync-buffer 3000','--sync-prep 2500','--cycles 1000']:self.assertIn(part,command)
        bad=subprocess.run(cmd+['--serial','1050211584'],capture_output=True,text=True)
        self.assertNotEqual(bad.returncode,0);self.assertIn('conflicts',bad.stderr)

    def test_exp2_cached_wrong_pac_rejected_without_flash(self):
        cmd=['bash',str(API/'brrs_exp2_capture.sh'),'rx','32','1','settings_cache_test','--lead','17','--pac','8','--serial','1050270933','--no-build','--build-only']
        r=subprocess.run(cmd,capture_output=True,text=True,env={**os.environ,'ARM_NM':'/Applications/SEGGER/SEGGER Embedded Studio 8.28/gcc/arm-none-eabi/bin/nm'})
        self.assertNotEqual(r.returncode,0);self.assertIn('PAC mismatch',r.stderr)

if __name__=='__main__': unittest.main(verbosity=2)
