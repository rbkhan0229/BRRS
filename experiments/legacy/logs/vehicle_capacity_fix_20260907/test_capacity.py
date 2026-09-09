"""No hardware access. Synthetic control wrappers around historical raw logs."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BASE=Path('/Users/songchieon/Desktop/DWM3000')
API=BASE/'DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API'
sys.path.insert(0,str(API))
import brrs_suite_manifest as manifest
import brrs_exp4_capacity as capacity
from brrs_suite_case import sha

class CapacityTests(unittest.TestCase):
    def setUp(self):
        self.m=manifest.load(API/'brrs_vehicle_manifest.json')
        self.m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':25,'8':25},'evidence':'TEST ONLY, not selected leads'}
        self.cases=manifest.plan(self.m,'exp4',capacity_candidates=True)
        self.group=[c for c in self.cases if c['conditions']['preamble']==128 and c['conditions']['rx_pac']==8]
        self.by_k={len(c['conditions']['slot_owners']):c for c in self.group}

    def observations(self, **verdicts):
        return {self.by_k[int(k)]['id']:{'verdict':v} for k,v in verdicts.items()}

    def test_capacity_ranges_and_both_pacs(self):
        self.assertEqual(len(self.cases),46)
        self.assertEqual([len(c['conditions']['slot_owners']) for c in self.group],[6,10,9,8,7])
        self.assertEqual(len(manifest.plan(self.m,'exp4')),16)
        for c in self.cases:
            p=c['conditions']; self.assertEqual(set(p['slot_owners']),set('234567'))
            self.assertEqual(len(c['jobs']),7)
            self.assertLessEqual(len(p['slot_owners']),p['max_slots'])

    def test_m128_window_and_distinct_pac_leads(self):
        self.m['lead_selection']['lead_us_by_pac']['4']=17
        for c in manifest.plan(self.m,'exp4',capacity_candidates=True):
            p=c['conditions']
            if p['preamble']!=128: continue
            self.assertEqual(p['rx_window_us'],212 if p['rx_pac']==4 else 220)
            self.assertEqual(p['fwto_uus'],207 if p['rx_pac']==4 else 215)
            for j in c['jobs']:
                a=j['build_only_argv'];self.assertEqual(a[a.index('--sequence')+1],p['slot_owners'])
                self.assertEqual(a[a.index('--lead')+1],str(p['lead_us']))

    def test_invalid_intermediate_owner_map_rejected(self):
        self.m['exp4']['sequences_by_preamble_slotcount']['128:9']='222222222'
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'manifest.json';p.write_text(json.dumps(self.m))
            with self.assertRaises(ValueError):manifest.load(p)

    def test_baseline_then_max_then_next_lower(self):
        for seen,k in [({},6),({'6':'PASS'},10),({'6':'PASS','10':'FAIL_PER'},9)]:
            d=capacity.next_case(self.group,self.observations(**seen))
            self.assertEqual(d['status'],'NEXT');self.assertEqual(d['case_id'],self.by_k[k]['id'])

    def test_failed_baseline_does_not_assume_monotonic_per(self):
        d=capacity.next_case(self.group,self.observations(**{'6':'FAIL_PER'}))
        self.assertEqual(d['case_id'],self.by_k[10]['id'])
        d=capacity.next_case(self.group,self.observations(**{'6':'FAIL_PER','10':'PASS'}))
        self.assertEqual(d['slots'],10);self.assertFalse(d['paper_capacity_validated'])

    def test_lower_pass_does_not_hide_unmeasured_higher_load(self):
        d=capacity.next_case(self.group,self.observations(**{'6':'PASS','8':'PASS'}))
        self.assertEqual(d['case_id'],self.by_k[10]['id'])
        d=capacity.next_case(self.group,self.observations(**{'6':'PASS','10':'FAIL_PER','9':'FAIL_PER','8':'PASS'}))
        self.assertEqual((d['status'],d['slots']),('SCREENING_MAX_FOUND',8))

    def test_invalid_stops_instead_of_reducing_load(self):
        d=capacity.next_case(self.group,self.observations(**{'6':'PASS','10':'INVALID'}))
        self.assertEqual(d['status'],'STOP_INVALID')

    def test_all_fail_does_not_claim_smaller_node_capacity(self):
        d=capacity.next_case(self.group,self.observations(**{str(k):'FAIL_PER' for k in self.by_k}))
        self.assertEqual(d['status'],'NO_PASS_IN_CONFIGURED_RANGE')

    def test_strict_per_threshold_and_weak_node(self):
        nodes={str(i):{'offered':1000,'rx':1000,'tx_success':1000} for i in range(6)}
        nodes['0']['rx']=991;self.assertEqual(capacity.per_goal(nodes),'PASS')
        nodes['0']['rx']=990;self.assertEqual(capacity.per_goal(nodes),'FAIL_PER')
        nodes['0']['rx']=0;self.assertEqual(capacity.per_goal(nodes),'FAIL_PER')

    def test_zero_and_impossible_counts_rejected(self):
        for offered,rx,sent in [(0,0,0),(1000,0,1000),(1000,999,998),(1000,1001,1001),(1000,-1,1000),(1000,1.5,1000)]:
            with self.subTest(counts=(offered,rx,sent)),self.assertRaises(ValueError):
                capacity.per_goal({'serial':{'offered':offered,'rx':rx,'tx_success':sent}})

    def test_beacon_misses_remain_in_offered_denominator(self):
        self.assertEqual(capacity.per_goal({'serial':{'offered':1000,'rx':989,'tx_success':989}}),'FAIL_PER')

    def make_fixture_bundle(self, root):
        # All supervisor/flash evidence here is fabricated TEST data. Raw UART
        # logs are read from an existing valid collection, never changed.
        c=copy.deepcopy(next(c for c in self.cases if c['id']=='exp4_m32_pac4_l25_k13'))
        c['boards']=self.m['boards'];c['TEST_ONLY']=True
        states={side:{'status':'COLLECTION_METADATA_AND_FLASH_PASS','workers':{},'inactive_halted_after':{}} for side in ['local','remote']}
        historical=BASE/'logs/exp4_home_s6_multislot_pac_ab_20260907/R13P4_r1'
        for j in c['jobs']:
            role=j['physical_role'];side='local' if role=='init' else 'remote'
            raw=historical/('init' if side=='local' else 'tx')/(role+'.log')
            out=root/'results'/side;out.mkdir(parents=True,exist_ok=True)
            dest=out/(role+'.log');dest.write_bytes(raw.read_bytes())
            hexf=root/(role+'.hex');hexf.write_text('TEST FIXTURE: NOT A FLASHABLE IMAGE\n')
            j['hex']=hexf.name;j['hex_sha256']=sha(hexf)
            meta={'serial':j['serial'],'physical_role':role,'logical_node':str(j['logical_node']),
                'suite_case_id':c['id'],'suite_conditions_sha256':c['conditions_sha256'],
                'suite_manifest_sha256':j['environment']['BRRS_SUITE_MANIFEST_SHA256'],
                'firmware_sha256':j['hex_sha256'],'raw_sha256':sha(dest),'collection_status':'PASS'}
            (out/(role+'.meta.txt')).write_text(''.join(f'{k}={v}\n' for k,v in meta.items()))
            states[side]['workers'][role]={'serial':j['serial'],'exit_code':0,'raw_sha256':sha(dest),
                'readback':{'status':'PASS','serial':j['serial'],'hex_sha256':j['hex_sha256']}}
        (root/'case.json').write_text(json.dumps(c))
        files=[root/'case.json',*root.glob('*.hex')]
        (root/'payload_hashes.json').write_text(json.dumps({p.name:sha(p) for p in files}))
        for side,state in states.items():(root/'results'/side/'status.json').write_text(json.dumps(state))
        (root/'results/orchestration.json').write_text(json.dumps({'status':'COLLECTION_AND_READBACK_PASS_PER_PENDING','payload_index_sha256':sha(root/'payload_hashes.json')}))
        return next(x for x in self.cases if x['id']==c['id'])

    def test_full_historical_log_parsing_in_test_control_wrapper(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);c=self.make_fixture_bundle(root)
            r=capacity.assess_bundle(root,c)
            self.assertEqual(r['verdict'],'FAIL_PER')
            self.assertEqual(r['nodes_by_serial']['1050282818']['per_percent'],7.15)
            self.assertEqual(r['aggregate']['rx'],12857)
            self.assertAlmostEqual(r['aggregate']['app_goodput_bps'],164569.63,places=1)

    def test_metadata_hash_marker_control_and_condition_fail_closed(self):
        for fault in ['raw','metadata','ready','control','condition','assignment']:
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as td:
                root=Path(td);c=self.make_fixture_bundle(root)
                if fault=='raw':
                    p=root/'results/local/init.log';p.write_text(p.read_text()+'changed\n')
                elif fault=='metadata':
                    p=root/'results/remote/N4.meta.txt';p.write_text(p.read_text().replace('serial=1050282818','serial=123'))
                elif fault=='ready':
                    with self.assertRaises(ValueError):capacity.strict_lines('EXP_LOG_READY,channel=1\nEXP_LOG_READY,channel=1\n===== END STATS =====')
                    continue
                elif fault=='control':
                    p=root/'results/orchestration.json';d=json.loads(p.read_text());d['status']='TIMEOUT';p.write_text(json.dumps(d))
                elif fault=='condition':c=copy.deepcopy(c);c['conditions']['lead_us']=24
                else:c=copy.deepcopy(c);c['jobs'][1]['serial']='123'
                with self.assertRaises(ValueError):capacity.assess_bundle(root,c)

    def test_duplicate_input_cli_stops(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)/'bundle';root.mkdir();self.make_fixture_bundle(root)
            path=Path(td)/'manifest.json';path.write_text(json.dumps(self.m))
            r=subprocess.run([sys.executable,str(API/'brrs_exp4_capacity.py'),str(path),'--bundles',str(root),str(root)],capture_output=True,text=True)
            self.assertEqual(r.returncode,2,r.stderr)
            d=json.loads(r.stdout);self.assertEqual(len(d['rejected']),1)
            self.assertTrue(all(x['decision']['status']=='STOP_INVALID_INPUT' for x in d['groups']))

if __name__=='__main__':unittest.main(verbosity=2)
