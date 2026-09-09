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

API=Path(__file__).resolve().parents[1];BASE=API.parents[2]
sys.path.insert(0,str(API))
import brrs_suite_manifest as manifest
import brrs_suite_paper as paper
import brrs_suite_case as case
import brrs_suite_results as results
import brrs_suite_leads as leads
import brrs_suite_campaign as campaign
from brrs_suite_evidence import read_evidence

def frozen():
    m=manifest.load(API/'brrs_vehicle_manifest.json')
    m['lead_selection']={'frozen':True,'lead_us_by_pac':{'4':25,'8':25},'evidence':'TEST ONLY, not field selected'}
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
        for stage,n in {'stage0':82,'exp1':40,'exp2':24,'exp3':9,'exp4':696,'exp5':3}.items():
            c=manifest.plan(self.m,stage,profile='paper')
            self.assertEqual(len(c),n);self.assertEqual(len({x['id'] for x in c}),n)
        self.assertEqual(len(manifest.plan(self.m,'exp4')),16)

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
                    if stage!='exp4' and j['logical_node']==2:self.assertEqual(j['physical_role'],'N4')

    def test_incomplete_rotation_cycle_rejected(self):
        m=copy.deepcopy(self.m);m['paper']['repeats_by_stage']['exp4']=10
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
        old=BASE/'logs/exp4_home_s6_multislot_pac_ab_20260907/R13P4_r1'
        raw={'init':(old/'init/init.log').read_text()}
        raw.update({f'N{i}':(old/f'tx/N{i}.log').read_text() for i in range(2,8)})
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
        for pac in [4,8]:
            center=17 if pac==4 else 25
            for lead in self.m['stage0']['leads_us']:
                obs[f'{pac}:{lead}']={'conditions':{'rx_pac':pac,'lead_us':lead},'worst_node_per_percent':0 if abs(lead-center)<=1 else 2}
        return {'rejected':[],'groups':[{'status':'PASS'}],'observations':obs}

    def test_distinct_pac_candidates_and_confirmation_plan(self):
        self.assertEqual(leads.candidates(self.m,self.grid_report()),{'4':17,'8':25})
        m=copy.deepcopy(self.m);m['lead_candidates_us_by_pac']={'4':17,'8':25}
        c=manifest.plan(m,'stage0',profile='paper',confirmation=True);self.assertEqual(len(c),30)
        self.assertEqual({x['conditions']['lead_us'] for x in c if x['conditions']['rx_pac']==4},{16,17,18})
        self.assertEqual({x['conditions']['run'] for x in c},{1,2,3,4,5})

    def test_missing_or_unreliable_grid_does_not_select(self):
        r=self.grid_report();r['groups'][0]['status']='INCOMPLETE'
        with self.assertRaises(ValueError):leads.candidates(self.m,r)
        r=self.grid_report()
        for v in r['observations'].values():v['worst_node_per_percent']=1
        with self.assertRaises(ValueError):leads.candidates(self.m,r)

    def test_confirmation_fail_or_uncertainty_does_not_freeze(self):
        m=copy.deepcopy(self.m);m['lead_candidates_us_by_pac']={'4':17,'8':25}
        r={'rejected':[],'groups':[{'status':'PASS','nodes_by_serial':{'tx':{'per_wilson95_percent':[0,.1]}}}],
            'observations':{str(i):{'conditions':{'rx_pac':pac,'lead_us':l}} for i,(pac,l) in enumerate((p,l) for p in [4,8] for l in paper.confirmation_leads(m,p))}}
        self.assertEqual(leads.freeze(m,r),{'4':17,'8':25})
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
