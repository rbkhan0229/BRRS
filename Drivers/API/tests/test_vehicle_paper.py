"""Offline tests. Any control/readback metadata in fixtures is synthetic."""
from collections import Counter,defaultdict
import copy
import io
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

API=Path(__file__).resolve().parents[1];FIXTURES=Path(__file__).resolve().parent/'fixtures'
sys.path.insert(0,str(API))
import brrs_suite_manifest as manifest
import brrs_suite_paper as paper
import brrs_suite_case as case
import brrs_suite_results as results
import brrs_suite_leads as leads
import brrs_suite_campaign as campaign
import brrs_exp4_verify as exp4_verify
from brrs_suite_evidence import read_evidence

def frozen():
    m=manifest.load(API/'brrs_vehicle_manifest.json')
    values={'m32_pac4':25,'m32_pac8':25,'m1024_pac32':25}
    m['lead_selection']={'frozen':True,'lead_us_by_config':values,'evidence':'TEST ONLY, not field selected'}
    m['lead_candidates_us_by_config']=values
    return m

def fake_bundle(root,m,c,raw):
    """TEST ONLY: valid raw logs wrapped in fabricated controller evidence."""
    c=copy.deepcopy(c);c.update(boards=m['boards'],TEST_ONLY=True,runtime_schema_version=2,firmware_source_sha256={'fixture':'TEST_ONLY'})
    (root/'board_manifest.json').write_text(json.dumps(m))
    states={s:{'status':'COLLECTION_METADATA_AND_FLASH_PASS','case_id':c['id'],'workers':{},'inactive_halted_after':{}} for s in ['local','remote']}
    for j in c['jobs']:
        p=c['conditions'];role=j['physical_role'];side='local' if role=='init' else 'remote'
        logical='init' if j['logical_node']==1 else f'N{j["logical_node"]}'
        hf=root/(f'exp4_{p["preamble"]}_s{p.get("sensors",1)}_{logical}.hex' if p['stage']=='exp4' else role+'.hex')
        hf.write_text(':0100000001FE\n:00000001FF\n');j['hex']=hf.name;j['hex_sha256']=case.sha(hf)
        out=root/'results'/side;out.mkdir(parents=True,exist_ok=True)
        log=out/(role+'.log');log.write_text(raw[logical])
        meta={'serial':j['serial'],'physical_role':role,'logical_node':str(j['logical_node']),'suite_case_id':c['id'],
            'suite_conditions_sha256':c['conditions_sha256'],'suite_manifest_sha256':j['environment']['BRRS_SUITE_MANIFEST_SHA256'],
            'firmware_sha256':j['hex_sha256'],'raw_sha256':case.sha(log),'collection_status':'PASS','run_number':str(p['run']),
            'suite_profile':p.get('profile','preparation'),'suite_block':str(p['run']),'suite_rotation_index':str(p.get('rotation_index',0)),
            'physical_location':j['location'],'suite_assignment_sha256':c.get('assignment_sha256','none')}
        (out/(role+'.meta.txt')).write_text(''.join(f'{k}={v}\n' for k,v in meta.items()))
        states[side]['workers'][role]={'serial':j['serial'],'exit_code':0,'raw_sha256':case.sha(log),
            'started_at':'2026-09-07T09:00:01+00:00','readback':{'serial':j['serial'],'hex_sha256':j['hex_sha256'],'status':'PASS'}}
    states['remote']['inactive_halted_after']={r:True for r in c['inactive_tx_roles']}
    (root/'case.json').write_text(json.dumps(c))
    paths=[root/'case.json',root/'board_manifest.json',*root.glob('*.hex')]
    (root/'payload_hashes.json').write_text(json.dumps({p.name:case.sha(p) for p in paths}))
    for side,s in states.items():(root/'results'/side/'status.json').write_text(json.dumps(s))
    (root/'results/orchestration.json').write_text(json.dumps({'status':'COLLECTION_AND_READBACK_PASS_PER_PENDING','case_id':c['id'],
        'payload_index_sha256':case.sha(root/'payload_hashes.json'),'all_tx_ready_at':'2026-09-07T09:00:00+00:00'}))

class PaperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m=frozen();cls.exp4=manifest.plan(cls.m,'exp4',profile='paper')

    def test_paper_repetitions_unique_ids(self):
        for stage,n in {'stage0':123,'exp1':40,'exp2':144,'exp3':9,'exp4':696,'exp5':18}.items():
            c=manifest.plan(self.m,stage,profile='paper')
            self.assertEqual(len(c),n);self.assertEqual(len({x['id'] for x in c}),n)
        self.assertEqual(len(manifest.plan(self.m,'exp4')),16)

    def test_named_profiles_have_declared_scope_and_counts(self):
        expected={
            'standard':({'stage0':123,'exp1':0,'exp2':54,'exp3':3,'exp4':276,'exp5':18},15),
            'essential':({'stage0':123,'exp1':40,'exp2':72,'exp3':9,'exp4':48,'exp5':18},15),
            'lite':({'stage0':123,'exp1':8,'exp2':24,'exp3':3,'exp4':8,'exp5':6},3),
            'full':({'stage0':123,'exp1':40,'exp2':144,'exp3':9,'exp4':696,'exp5':18},15),
        }
        for profile,(counts,confirmation) in expected.items():
            with self.subTest(profile=profile):
                planned={stage:manifest.plan(self.m,stage,profile=profile) for stage in counts}
                self.assertEqual({stage:len(cases) for stage,cases in planned.items()},counts)
                self.assertEqual(len(manifest.plan(self.m,'stage0',profile=profile,confirmation=True)),confirmation)
                self.assertTrue(all(c['id'].startswith(profile+'_') for cases in planned.values() for c in cases))
                self.assertTrue(all(c['conditions']['profile']==profile for cases in planned.values() for c in cases))
        essential=manifest.plan(self.m,'exp4',profile='essential')
        self.assertEqual({c['conditions']['sensors'] for c in essential},{6})
        self.assertEqual({c['conditions']['preamble'] for c in essential},{32,256})
        self.assertEqual({c['conditions']['rotation_index'] for c in essential},set(range(6)))
        self.assertEqual({len(c['conditions']['slot_owners']) for c in essential},{6,8,13})
        lite=manifest.plan(self.m,'exp4',profile='lite')
        self.assertEqual({c['conditions']['run'] for c in lite},{1})
        self.assertEqual({c['conditions']['rotation_index'] for c in lite},{0})
        for stage in ['stage0','exp1','exp2','exp3','exp4','exp5']:
            essential_conditions={c['condition_id'] for c in manifest.plan(self.m,stage,profile='essential')}
            lite_conditions={c['condition_id'] for c in manifest.plan(self.m,stage,profile='lite')}
            self.assertEqual(lite_conditions,essential_conditions)
        essential_confirmation={c['condition_id'] for c in manifest.plan(
            self.m,'stage0',profile='essential',confirmation=True)}
        lite_confirmation={c['condition_id'] for c in manifest.plan(
            self.m,'stage0',profile='lite',confirmation=True)}
        self.assertEqual(lite_confirmation,essential_confirmation)

    def test_standard_folds_exp1_into_rotating_exp4_s1(self):
        self.assertEqual(manifest.plan(self.m,'exp1',profile='standard'),[])
        planned=manifest.plan(self.m,'exp4',profile='standard')
        self.assertEqual({c['conditions']['sensors'] for c in planned},set(range(1,7)))
        for sensors in range(1,7):
            selected=[c for c in planned if c['conditions']['sensors']==sensors]
            expected={32,64,128,256} if sensors==1 else {32,256}
            self.assertEqual({c['conditions']['preamble'] for c in selected},expected)
            self.assertEqual({c['conditions']['rotation_index'] for c in selected},set(range(6)))
            self.assertEqual({c['conditions']['rx_pac'] for c in selected
                              if c['conditions']['preamble']==32},{4,8})
            self.assertTrue(all(c['conditions']['rx_pac']==8 for c in selected
                                if c['conditions']['preamble']!=32))
        s1=[c for c in planned if c['conditions']['sensors']==1]
        physical_by_block={
            c['conditions']['run']:c['conditions']['active_physical_roles'][0]
            for c in s1 if c['conditions']['preamble']==32 and c['conditions']['rx_pac']==4
        }
        self.assertEqual(physical_by_block,{1:'N2',2:'N3',3:'N4',4:'N5',5:'N6',6:'N7',
                                            7:'N2',8:'N3',9:'N4',10:'N5',11:'N6',12:'N7'})
        grouped=defaultdict(set)
        for c in s1:
            grouped[c['conditions']['run']].add(tuple(c['conditions']['active_physical_roles']))
        self.assertTrue(all(len(mappings)==1 for mappings in grouped.values()))

    def test_stage0_and_exp5_use_distinct_phy_specific_leads(self):
        m=copy.deepcopy(self.m)
        values={'m32_pac4':17,'m32_pac8':25,'m1024_pac32':21}
        m['lead_selection']['lead_us_by_config']=values
        m['lead_candidates_us_by_config']=values
        stage0=manifest.plan(m,'stage0',profile='standard')
        self.assertEqual({(c['conditions']['preamble'],c['conditions']['rx_pac']) for c in stage0},
                         {(32,4),(32,8),(1024,32)})
        pac32=next(c for c in stage0 if c['conditions']['preamble']==1024)
        self.assertIn('--preamble',pac32['jobs'][0]['argv'])
        self.assertEqual(pac32['jobs'][0]['argv'][pac32['jobs'][0]['argv'].index('--preamble')+1],'1024')
        exp5=manifest.plan(m,'exp5',profile='standard')
        self.assertEqual({c['conditions']['lead_us'] for c in exp5},{21})
        self.assertEqual({c['conditions']['rx_pac'] for c in exp5},{32})

    def test_legacy_paper_manifest_and_ids_remain_reproducible(self):
        legacy=copy.deepcopy(self.m)
        legacy['stage0'].pop('phy_configs')
        legacy['exp5']={'preamble':1024,'pac':32,'lead_from_pac':8}
        legacy['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':25,'8':25},'evidence':'TEST ONLY'}
        legacy['lead_candidates_us_by_pac']={'4':25,'8':25}
        legacy.pop('lead_candidates_us_by_config')
        for config in legacy['profiles'].values():
            if 'stage_filters' in config:
                config['stage_filters']['stage0']={'pacs':[4,8]}
        legacy['paper']=legacy.pop('profiles')['full']
        cases=manifest.plan(legacy,'exp4',profile='paper')
        self.assertEqual(len(cases),696)
        self.assertTrue(all(c['id'].startswith('paper_') for c in cases))
        self.assertEqual({c['conditions']['profile'] for c in cases},{'paper'})

    def test_pre_standard_named_profiles_remain_readable(self):
        old=copy.deepcopy(self.m)
        old['profiles'].pop('standard')
        paper.validate(old)
        self.assertEqual(len(manifest.plan(old,'exp4',profile='essential')),48)

    def test_profile_hierarchy_is_fail_closed(self):
        changed=copy.deepcopy(self.m)
        changed['profiles']['lite']['stage_filters']['exp2']['preambles'].append(64)
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['lite']['repeats_by_stage']['exp1']=2
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['full']['s6_slot_counts_by_preamble']['32'].remove(13)
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['standard']['repeats_by_stage']['exp1']=1
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['standard']['exp4_preambles_by_active_tx']['2'].append(64)
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['standard']['stage_filters']['exp4']['pacs_by_preamble']['256'].append(4)
        with self.assertRaises(ValueError):paper.validate(changed)
        changed=copy.deepcopy(self.m)
        changed['profiles']['standard']['repeats_by_stage']['exp4']=6
        with self.assertRaises(ValueError):paper.validate(changed)

    def test_each_stage_completes_one_condition_block_before_repeating(self):
        for stage in ['exp1','exp2','exp3','exp4','exp5']:
            planned=manifest.plan(self.m,stage,profile='paper')
            runs=[c['conditions']['run'] for c in planned]
            self.assertEqual(runs,sorted(runs))
            per_run=defaultdict(list)
            for c in planned:per_run[c['conditions']['run']].append(c['condition_id'])
            self.assertTrue(all(len(v)==len(set(v)) for v in per_run.values()))
            self.assertEqual(len({len(v) for v in per_run.values()}),1)

    def test_all_sensors_balanced_logical_roles_and_s1_n4(self):
        for sensors in range(1,7):
            c=[c for c in self.exp4 if c['conditions']['sensors']==sensors and c['conditions']['preamble']==32 and c['conditions']['rx_pac']==8 and len(c['conditions']['slot_owners'])==sensors]
            counts=Counter((j['serial'],j['logical_node']) for x in c for j in x['jobs'] if j['logical_node']!=1)
            if sensors==1:self.assertEqual(counts,{('1050282818',2):12})
            else:
                self.assertEqual(len(counts),6*sensors);self.assertEqual(set(counts.values()),{2})

    def test_comparisons_share_mapping_within_block_and_preserve_rx(self):
        groups=defaultdict(set)
        for c in self.exp4:
            p=c['conditions'];mapping=tuple((j['logical_node'],j['serial']) for j in c['jobs'])
            self.assertEqual(mapping[0],(1,'1050270933'))
            groups[(p['sensors'],p['run'])].add(mapping)
            self.assertEqual(len(c['inactive_tx_roles']),6-p['sensors'])
        self.assertTrue(all(len(v)==1 for v in groups.values()))

    def test_original_installation_map_not_sorted_serials(self):
        c=next(c for c in self.exp4 if c['conditions']['sensors']==6 and c['conditions']['run']==1)
        self.assertEqual([j['physical_role'] for j in c['jobs']],['init','N2','N3','N4','N5','N6','N7'])
        c=next(c for c in self.exp4 if c['conditions']['sensors']==6 and c['conditions']['run']==2)
        self.assertEqual([j['physical_role'] for j in c['jobs']],['init','N3','N4','N5','N6','N7','N2'])

    def test_cli_role_run_serial_and_metadata(self):
        for stage in ['stage0','exp1','exp2','exp3','exp4','exp5']:
            for c in manifest.plan(self.m,stage,profile='paper'):
                p=c['conditions'];idx=5 if stage=='exp4' else 3 if stage=='exp5' else 4
                for j in c['jobs']:
                    self.assertEqual(j['argv'][idx],str(p['run']))
                    self.assertEqual(j['argv'][j['argv'].index('--serial')+1],j['serial'])
                    self.assertEqual(j['environment']['BRRS_SUITE_PHYSICAL_ROLE'],j['physical_role'])
                    self.assertEqual(j['environment']['BRRS_SUITE_MANIFEST_SHA256'],paper.digest(self.m))
                    if stage in ['stage0','exp1','exp3'] and j['logical_node']==2:self.assertEqual(j['physical_role'],'N4')
                    if stage in ['exp2','exp5'] and j['logical_node']==2:self.assertEqual(j['physical_role'],p['link_tx_role'])

    def test_incomplete_rotation_cycle_rejected(self):
        m=copy.deepcopy(self.m);m['profiles']['full']['repeats_by_stage']['exp4']=10
        with self.assertRaises(ValueError):paper.validate(m)

    def test_preparation_s1_also_keeps_designated_n4(self):
        m=copy.deepcopy(self.m);m['exp4']['sensors']=1;m['exp4'].pop('capacity_search')
        m['exp4']['slot_counts_by_preamble']={str(p):[1] for p in m['exp4']['preambles']}
        self.assertEqual(manifest.serial_for(m,'exp4','N2'),'1050282818')
        self.assertEqual(manifest.fixed_assignments(m,1,['1050282818']),[('N2','1050282818')])
        for c in manifest.plan(m,'exp4'):
            self.assertEqual((c['jobs'][1]['physical_role'],c['jobs'][1]['logical_node']),('N4',2))
            self.assertEqual(len(c['inactive_tx_roles']),5)

    def test_standalone_s1_runner_uses_n4_serial(self):
        cmd=['bash',str(API/'brrs_run_experiment.sh'),'exp4','N2','TEST_ONLY_s1','--sensors','1','--preambles','32','--guard','250','--lead','25','--pac','8','--slotted-rx','--spi-opt','--dry-run']
        r=subprocess.run(cmd,capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr);self.assertIn('--serial 1050282818',r.stdout)

    def test_unified_runner_propagates_beacon_preamble_to_every_stage(self):
        cases = {
            'stage0': ['rx','TEST_ONLY','--leads','15'],
            'exp1': ['rx','TEST_ONLY','--preambles','32','--pac','8'],
            'exp2': ['rx','TEST_ONLY','--preambles','32'],
            'exp3': ['rx','TEST_ONLY'],
            'exp4': ['init','TEST_ONLY','--sensors','1','--preambles','32'],
            'exp5': ['rx','TEST_ONLY'],
        }
        for stage, tail in cases.items():
            with self.subTest(stage=stage):
                cmd=['bash',str(API/'brrs_run_experiment.sh'),stage,*tail,
                     '--beacon-preamble','256','--dry-run']
                r=subprocess.run(cmd,capture_output=True,text=True)
                self.assertEqual(r.returncode,0,r.stderr)
                command_lines=[line for line in r.stdout.splitlines()
                               if 'brrs_' in line and '_capture.sh' in line]
                self.assertTrue(command_lines,r.stdout)
                self.assertTrue(all('--beacon-preamble 256' in line
                                    for line in command_lines),r.stdout)

    def test_all_connected_probes_checked_while_subset_active(self):
        c=copy.deepcopy(next(c for c in self.exp4 if c['conditions']['sensors']==2));c['boards']=self.m['boards']
        actual=[SimpleNamespace(SerialNumber=int(self.m['boards'][r]['serial'])) for r in ['N7','N3','N6','N2','N5','N4']]
        stub=SimpleNamespace(JLink=lambda:SimpleNamespace(connected_emulators=lambda:actual))
        with patch.dict(sys.modules,{'pylink':stub}):
            roles,jobs=case.probe_check(c,'remote');self.assertEqual(len(roles),6);self.assertEqual(len(jobs),2)
            actual.pop()
            with self.assertRaises(RuntimeError):case.probe_check(c,'remote')

    def rotated_fixture(self,root):
        c=next(c for c in self.exp4 if c['id']=='paper_exp4_m32_pac4_l25_k13_s6_b02')
        raw={'init':(FIXTURES/'exp4_init.txt').read_text()}
        raw.update({f'N{i}':(FIXTURES/f'exp4_N{i}.txt').read_text() for i in range(2,8)})
        fake_bundle(root,self.m,c,raw);return c

    def test_actual_log_loss_follows_physical_serial_after_rotation(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);self.rotated_fixture(root);r=results.assess(root)
            self.assertEqual(r['verdict'],'FAIL_PER')
            self.assertEqual(r['nodes_by_serial']['1050208509']['per_percent'],7.15)
            self.assertEqual(r['nodes_by_serial']['1050282818']['per_percent'],0)

    def test_stale_rotation_metadata_or_ready_order_rejected(self):
        for fault in ['rotation','ready','extra_worker']:
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as td:
                root=Path(td);c=self.rotated_fixture(root)
                if fault=='rotation':
                    p=root/'results/remote/N4.meta.txt';p.write_text(p.read_text().replace('suite_rotation_index=1','suite_rotation_index=0'))
                elif fault=='ready':
                    p=root/'results/orchestration.json';v=json.loads(p.read_text());v['all_tx_ready_at']='2026-09-07T09:00:02+00:00';p.write_text(json.dumps(v))
                else:
                    p=root/'results/remote/status.json';v=json.loads(p.read_text());v['workers']['EXTRA']={};p.write_text(json.dumps(v))
                with self.assertRaises(ValueError):read_evidence(root,c)

    def test_incomplete_and_bad_run_not_hidden_by_pooled_per(self):
        c=manifest.plan(self.m,'exp1',profile='paper');c=[x for x in c if x['condition_id']==c[0]['condition_id']]
        observations={}
        for i,x in enumerate(c):
            rx=1970 if i==0 else 2000
            observations[x['id']]={'case_id':x['id'],'verdict':'FAIL_PER' if i==0 else 'PASS','firmware_source_sha256':{'C':'same'},
                'nodes_by_serial':{'1050282818':{'physical_role':'N4','logical_node':2,'location':'driver_seat','offered':2000,'rx':rx,'tx_success':2000,'per_percent':100*(2000-rx)/2000}}}
        g=results.aggregate(c,observations)[0]
        self.assertEqual(g['status'],'FAIL_PER');self.assertLess(g['nodes_by_serial']['1050282818']['per_percent'],1)
        observations.pop(c[-1]['id']);self.assertEqual(results.aggregate(c,observations)[0]['status'],'INCOMPLETE')
        observations[c[0]['id']]['verdict']='INVALID';self.assertEqual(results.aggregate(c,observations)[0]['status'],'INVALID')

    def test_exp3_measurement_rows_and_tick_units(self):
        p={'stage':'exp3','cycles':3,'variant':'B','lead_us':25}
        rx=['EXP_LOG_CONFIG_CSV,experiment=3,plen=32,lead_us=25,tail_us=0,target=3,cir=0',
            'EXP3_RX_DONE,variant=B,expected=3,rx=3,per_x1000=0,end_tx=3,status=PASS','EXP3_RX_RESULT_CSV,B,16,STD,26,3,3,0,0,100,PASS']
        tx=['EXP3_TX_RESULT,variant=B,attempts=3,success=3,captures=3,end=1,status=PASS','EXP3_TX_DUMP_DONE,variant=B,expected=3,count=3,status=PASS',
            *[f'EXP3_TX_CSV,{i},B,16,STD,26,1600,100000' for i in range(1,4)]]
        self.assertEqual(results.validate_exttxe(rx,tx,p)[2]['duration_mean_ns'],100000)
        with self.assertRaises(ValueError):results.validate_exttxe(rx,tx[:-1],p)
        with self.assertRaises(ValueError):results.validate_exttxe(rx,[s.replace('1600,100000','1600,100001') for s in tx],p)

    def cir_fixture(self,stage):
        m=1024 if stage=='exp5' else 128;p={'stage':stage,'cycles':3,'preamble':m,'lead_us':25,'rx_pac':32 if stage=='exp5' else 8}
        rx=[f'EXP_LOG_CONFIG_CSV,experiment={5 if stage=="exp5" else 2},plen={m},lead_us=25,tail_us=0,target=3,cir=1',
            f'EXP2_DONE,plen={m},expected=3,rx=3,valid_cir=3,dump_count=3,end_tx=3,collection=PASS,link=PASS,per_x1000=0,status=PASS',
            *[f'CIR_CSV,{i},{i},N2,{m},1,1,100,-80,-81,1,100,1,5,1000' for i in range(1,4)]]
        tx=[f'EXP2_TX_DONE,node=N2,plen={m},expected=3,attempts=3,success=3,delayed_late=0,beacon_config_errors=0,data_config_errors=0,end=1,collection=PASS,link=PASS,status=PASS',f'BRRS_BEACON_RX_CSV,version=3,m={m},data_psdu=26']
        if stage=='exp2':rx.append('EXP2_PHY_CONFIG_CSV,plen=128,pac=8,sfd_timeout=129,lead_us=25')
        else:
            rx.append('CIR_RAW_DUMP_DONE,plen=1024,count=3,samples_per_frame=300')
            for i in range(1,4):
                rx.append(f'CIR_RAW_HEADER,frame={i},plen=1024,n_samples=300')
                rx.extend(f'CIR_RAW,{i},{k},1,-1' for k in range(300))
        return rx,tx,p

    def test_exp2_missing_duplicate_cycle_or_wrong_pac(self):
        rx,tx,p=self.cir_fixture('exp2');self.assertEqual(results.validate_cir(rx,tx,p)[2]['cir_rows'],3)
        for bad in [rx[:-2]+rx[-1:],[s.replace('CIR_CSV,3,3,','CIR_CSV,3,2,') for s in rx],[s.replace('pac=8','pac=4') for s in rx]]:
            with self.assertRaises(ValueError):results.validate_cir(bad,tx,p)

    def test_exp5_complete_raw_samples_required(self):
        rx,tx,p=self.cir_fixture('exp5');self.assertEqual(results.validate_cir(rx,tx,p)[2]['raw_cir_frames'],3)
        for bad in [rx[:-1],rx+[rx[-1]]]:
            with self.assertRaises(ValueError):results.validate_cir(bad,tx,p)

    def grid_report(self):
        obs={}
        for preamble,pac,center in [(32,4,17),(32,8,25),(1024,32,21)]:
            for lead in self.m['stage0']['leads_us']:
                obs[f'{preamble}:{pac}:{lead}']={'conditions':{'preamble':preamble,'rx_pac':pac,'lead_us':lead},'worst_node_per_percent':0 if abs(lead-center)<=1 else 2}
        return {'rejected':[],'groups':[{'status':'PASS'}],'observations':obs}

    def test_distinct_pac_candidates_and_confirmation_plan(self):
        expected={'m32_pac4':17,'m32_pac8':25,'m1024_pac32':21}
        self.assertEqual(leads.candidates(self.m,self.grid_report()),expected)
        m=copy.deepcopy(self.m);m['lead_candidates_us_by_config']=expected
        c=manifest.plan(m,'stage0',profile='paper',confirmation=True);self.assertEqual(len(c),15)
        self.assertEqual({x['conditions']['lead_us'] for x in c if x['conditions']['rx_pac']==4},{17})
        self.assertEqual({x['conditions']['run'] for x in c},{1,2,3,4,5})

    def test_isolated_periodic_candidate_does_not_require_adjacent_passes(self):
        r=self.grid_report()
        for pac in [4,8,32]:
            for v in r['observations'].values():
                if v['conditions']['rx_pac']==pac:v['worst_node_per_percent']=2
        for preamble,pac,lead in [(32,4,17),(32,8,25),(1024,32,21)]:
            r['observations'][f'{preamble}:{pac}:{lead}']['worst_node_per_percent']=0
        self.assertEqual(leads.candidates(self.m,r),
                         {'m32_pac4':17,'m32_pac8':25,'m1024_pac32':21})

    def test_rev26_sensor_log_records_five_second_reacquisition_grace(self):
        lines=(FIXTURES/'exp4_N2.txt').read_text().splitlines()
        lines=[line.replace('rev=25','rev=26') for line in lines]
        lines=[line+',final_timeout_us=5000000' if line.startswith('EXP4_TX_BOOT_CSV,') else line for line in lines]
        detail=exp4_verify.verify_sensor(lines,32,6,2,250,3000,2500,'2345672345673')
        self.assertIn('beacon_loss=0/1000',detail)
        bad=[line.replace(',final_timeout_us=5000000','') for line in lines]
        with self.assertRaises(exp4_verify.VerificationError):
            exp4_verify.verify_sensor(bad,32,6,2,250,3000,2500,'2345672345673')

    def test_nondefault_beacon_preamble_is_runtime_verified(self):
        init_lines=(FIXTURES/'exp4_init.txt').read_text().splitlines()
        init_lines=[line.replace(',m=32,',',sync_m=256,m=32,')
                    if line.startswith('BRRS_BEACON_CONFIG_CSV,') else line
                    for line in init_lines]
        init_detail=exp4_verify.verify_init(
            init_lines,32,6,250,25,4,3000,2500,100,
            '2345672345673',spi_opt=True,slotted_rx=True,
            beacon_preamble=256)
        self.assertIn('rx=12857/13000',init_detail)
        with self.assertRaises(exp4_verify.VerificationError):
            exp4_verify.verify_init(
                init_lines,32,6,250,25,4,3000,2500,100,
                '2345672345673',spi_opt=True,slotted_rx=True,
                beacon_preamble=512)

        lines=(FIXTURES/'exp4_N2.txt').read_text().splitlines()
        lines=[line.replace('rev=25','rev=27') for line in lines]
        lines=[line+',sync_plen=256,final_timeout_us=5000000'
               if line.startswith('EXP4_TX_BOOT_CSV,') else line
               for line in lines]
        lines=[line.replace(',m=32,',',sync_m=256,m=32,')
               if line.startswith('BRRS_BEACON_RX_CSV,') else line
               for line in lines]
        detail=exp4_verify.verify_sensor(
            lines,32,6,2,250,3000,2500,'2345672345673',
            beacon_preamble=256)
        self.assertIn('beacon_loss=0/1000',detail)
        with self.assertRaises(exp4_verify.VerificationError):
            exp4_verify.verify_sensor(
                lines,32,6,2,250,3000,2500,'2345672345673',
                beacon_preamble=512)

    def test_missing_or_unreliable_grid_does_not_select(self):
        r=self.grid_report();r['groups'][0]['status']='INCOMPLETE'
        with self.assertRaises(ValueError):leads.candidates(self.m,r)
        r=self.grid_report()
        for v in r['observations'].values():v['worst_node_per_percent']=1
        with self.assertRaises(ValueError):leads.candidates(self.m,r)

    def test_confirmation_fail_or_uncertainty_does_not_freeze(self):
        candidates={'m32_pac4':17,'m32_pac8':25,'m1024_pac32':21}
        m=copy.deepcopy(self.m);m['lead_candidates_us_by_config']=candidates
        r={'rejected':[],'groups':[{'status':'PASS','nodes_by_serial':{'tx':{'per_wilson95_percent':[0,.1]}}}],
            'observations':{str(i):{'conditions':{'preamble':cfg['preamble'],'rx_pac':cfg['pac'],'lead_us':l}}
                for i,(cfg,l) in enumerate((cfg,l) for cfg in manifest.stage0_configs(m)
                                             for l in paper.confirmation_leads(m,cfg['preamble'],cfg['pac']))}}
        self.assertEqual(leads.freeze(m,r),candidates)
        r['groups'][0]['nodes_by_serial']['tx']['per_wilson95_percent'][1]=1
        with self.assertRaises(ValueError):leads.freeze(m,r)
        r['groups'][0]['status']='FAIL_PER'
        with self.assertRaises(ValueError):leads.freeze(m,r)

    def test_duplicate_and_explicit_contamination_exclusion(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);c=self.rotated_fixture(root)
            r=results.collect(self.m,[c],[root,root]);self.assertEqual(len(r['rejected']),1)
            ex={str(root):{'reason':'TEST user-reported disturbance','payload_index_sha256':case.sha(root/'payload_hashes.json')}}
            r=results.collect(self.m,[c],[root],ex);self.assertEqual(r['groups'][0]['status'],'INCOMPLETE');self.assertEqual(len(r['excluded']),1)

    def test_deployment_dry_run_never_starts_process(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);self.rotated_fixture(root)
            with patch.object(subprocess,'run',side_effect=AssertionError('network/process forbidden')):
                r=campaign.deploy(root,dry_run=True)
            self.assertFalse(r['network_access_performed'])

    def test_completed_campaign_resumes_without_repeating_capture(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);bundle=root/'bundle';bundle.mkdir();c=self.rotated_fixture(bundle)
            mp=root/'manifest.json';mp.write_text(json.dumps(self.m))
            spec={'manifest':str(mp),'manifest_sha256':case.sha(mp),'profile':'paper','stage':'exp4','confirmation':False,
                'capacity_candidates':False,'case_ids':[c['id']]}
            (root/'campaign.json').write_text(json.dumps({'spec':spec,'bundles':{c['id']:{'path':str(bundle),'payload_index_sha256':case.sha(bundle/'payload_hashes.json')}}}))
            with patch.object(subprocess,'run',side_effect=AssertionError('RF/network/process forbidden')),patch('sys.stdout',new_callable=io.StringIO) as stdout:
                campaign.run_campaign(SimpleNamespace(root=root,host=None,dry_run=True))
            r=json.loads(stdout.getvalue());self.assertEqual(r['actions'][0]['action'],'SKIP_COMPLETED')
            self.assertFalse(r['rf_execution_performed'])
            self.assertTrue(r['profile_slice'])
            self.assertFalse(r['groups'][0]['full_repetitions_complete'])
            self.assertFalse(r['full_stage_profile_complete'])

    def test_operator_paced_campaign_selects_only_next_incomplete_case(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);mp=root/'manifest.json';mp.write_text(json.dumps(self.m))
            selected=[c for c in self.exp4 if c['conditions']['run']==1][:2]
            raw={'init':(FIXTURES/'exp4_init.txt').read_text()}
            raw.update({f'N{i}':(FIXTURES/f'exp4_N{i}.txt').read_text() for i in range(2,8)})
            bundles={}
            for c in selected:
                bundle=root/c['id'];bundle.mkdir();fake_bundle(bundle,self.m,c,raw)
                shutil.rmtree(bundle/'results')
                bundles[c['id']]={'path':str(bundle),'payload_index_sha256':case.sha(bundle/'payload_hashes.json')}
            spec={'manifest':str(mp),'manifest_sha256':case.sha(mp),'profile':'paper','stage':'exp4',
                  'confirmation':False,'capacity_candidates':False,'blocks':[1],
                  'profile_case_count':696,'profile_slice':True,'case_ids':[c['id'] for c in selected]}
            (root/'campaign.json').write_text(json.dumps({'spec':spec,'bundles':bundles}))
            with patch.object(subprocess,'run',side_effect=AssertionError('RF/network/process forbidden')),patch('sys.stdout',new_callable=io.StringIO) as stdout:
                campaign.run_campaign(SimpleNamespace(root=root,host=None,dry_run=True,one_case=True))
            report=json.loads(stdout.getvalue())
            self.assertEqual(report['new_cases_started'],1)
            self.assertTrue(report['operator_paced'])
            self.assertEqual(report['actions'][0],{'case_id':selected[0]['id'],'action':'DEPLOY_THEN_RUN'})
            self.assertEqual(report['actions'][1]['action'],'PAUSED_AT_OPERATOR_LIMIT')
            self.assertEqual(report['actions'][1]['next_case_id'],selected[1]['id'])

    def test_other_environment_cannot_be_pooled(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);c=self.rotated_fixture(root);m=copy.deepcopy(self.m);m['environment']='different_environment'
            r=results.collect(m,[c],[root]);self.assertEqual(r['groups'][0]['status'],'INVALID')

    def test_supervisor_ready_gating_and_copy_failure_without_processes(self):
        for fault in [None,'no_tx_ready','copy_timeout']:
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as td:
                root=Path(td);self.rotated_fixture(root);shutil.rmtree(root/'results')
                started=[]
                def spawn(argv,stdout,**kw):
                    side='remote' if argv[0]=='ssh' else 'local';started.append(side)
                    if side=='remote' and fault!='no_tx_ready':stdout.write('ALL_READY remote\n');stdout.flush()
                    if side=='local':self.assertIn('ALL_READY remote',(root/'results/remote.supervisor.log').read_text())
                    return SimpleNamespace(poll=lambda:0,returncode=0,wait=lambda **kw:0)
                def command(argv,**kw):
                    if argv[0]=='scp' and fault=='copy_timeout':raise subprocess.TimeoutExpired(argv,30)
                    return SimpleNamespace(returncode=0)
                with patch.object(case.subprocess,'Popen',side_effect=spawn),patch.object(case.subprocess,'run',side_effect=command),\
                     patch.object(case.signal,'signal'),patch.object(results,'assess',return_value={'verdict':'PASS'}),patch('sys.stdout',new_callable=io.StringIO):
                    rc=case.run(SimpleNamespace(bundle=root,host=None))
                self.assertEqual(rc,0 if fault is None else 1)
                self.assertEqual(started,['remote'] if fault=='no_tx_ready' else ['remote','local'])
                state=json.loads((root/'results/orchestration.json').read_text())
                if fault:self.assertEqual(state['status'],'FAIL')
                else:self.assertEqual(state['assessment_verdict'],'PASS')

    def test_remote_extractor_locally_verifies_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)/'out';data=b'payload';h=__import__('hashlib').sha256(data).hexdigest()
            index=json.dumps({'file':h}).encode();indexhash=__import__('hashlib').sha256(index).hexdigest();buf=io.BytesIO()
            with tarfile.open(fileobj=buf,mode='w:gz') as tar:
                for name,content in [('file',data),('payload_hashes.json',index)]:
                    info=tarfile.TarInfo(name);info.size=len(content);tar.addfile(info,io.BytesIO(content))
            cmd=[sys.executable,'-c',campaign.REMOTE_EXTRACT,str(root),indexhash]
            r=subprocess.run(cmd,input=buf.getvalue(),capture_output=True);self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(subprocess.run(cmd,input=b'',capture_output=True).returncode,0)
            (root/'file').write_bytes(b'changed')
            self.assertNotEqual(subprocess.run(cmd,input=b'',capture_output=True).returncode,0)

if __name__=='__main__':unittest.main(verbosity=2)
