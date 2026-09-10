"""Offline physical-link planning, evidence and worker lifecycle regressions."""
from collections import Counter
import copy
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

API=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(API),str(API/'tests')]
import brrs_suite_manifest as manifest
import brrs_suite_case as case
import brrs_suite_results as results
import brrs_suite_campaign as campaign
import brrs_single_host as single
from test_vehicle_paper import fake_bundle, frozen

FIXTURES=Path(__file__).resolve().parent/'fixtures'

def real_raw(stage):
    return {'init':(FIXTURES/(stage+'_init.txt')).read_text(),
            'N2':(FIXTURES/(stage+'_tx.txt')).read_text()}

def synthetic_single_link_raw(p):
    """TEST ONLY: exercise standard evidence export and validators together."""
    n=p['cycles'];m=p['preamble'];lead=p['lead_us'];pac=p['rx_pac']
    ready='EXP_LOG_READY,channel=1\n'
    if p['stage'] in ['stage0','exp1']:
        rx=(f'BRRS_BEACON_CONFIG_CSV,sync_m=512\nEXP_LOG_CONFIG_CSV,sync_plen=512\n'
            f'EXP1_DONE,plen={m},lead_us={lead},tail_us=0,pac={pac},rx_mode=delayed,expected={n},rx={n},delayed_late=0,data_config_errors=0,end_tx=3,per_x1000=0,collection=PASS,status=PASS,link=PASS\n'
            f'N2: rx={n} expected={n} miss=0 PER=0.000% err=0\n')
        tx=(f'BRRS_BEACON_RX_CSV,m={m},sync_m=512\n'
            f'EXP1_TX_DONE,plen={m},expected={n},attempts={n},success={n},delayed_late=0,beacon_config_errors=0,data_config_errors=0,end=1,collection=PASS,status=PASS,link=PASS\n'
            f'My TX: success={n} attempts={n} delayed_late=0\n')
    else:
        v=p['variant'];sfd=16 if v=='B' else 8;phr='DTA' if v=='C' else 'STD'
        rx=(f'EXP_LOG_CONFIG_CSV,experiment=3,plen=32,lead_us={lead},tail_us=0,target={n},cir=0\n'
            f'EXP3_RX_DONE,variant={v},expected={n},rx={n},per_x1000=0,end_tx=3,status=PASS\n'
            f'EXP3_RX_RESULT_CSV,{v},{sfd},{phr},26,{n},{n},0,0,100,PASS\n')
        tx=(f'EXP3_TX_RESULT,variant={v},attempts={n},success={n},captures={n},end=1,status=PASS\n'
            f'EXP3_TX_DUMP_DONE,variant={v},expected={n},count={n},status=PASS\n'+
            ''.join(f'EXP3_TX_CSV,{i},{v},{sfd},{phr},26,1600,100000\n' for i in range(1,n+1)))
    return {'init':ready+rx,'N2':ready+tx}

def selected(m,stage,role,profile='paper'):
    return next(c for c in manifest.plan(m,stage,profile=profile)
                if c['conditions']['link_tx_role']==role and
                c['conditions']['rx_pac']==(8 if stage=='exp2' else 32) and
                c['conditions']['preamble']==(32 if stage=='exp2' else 1024))

class LinkTests(unittest.TestCase):
    def test_each_condition_has_six_serials_one_active_at_a_time(self):
        m=frozen()
        for stage,count in [('exp2',48),('exp5',6)]:
            cs=manifest.plan(m,stage)
            self.assertEqual(len(cs),count)
            self.assertEqual(len({c['id'] for c in cs}),count)
            self.assertEqual([c['conditions']['link_tx_role'] for c in cs[:6]],m['cir_link_tx_roles'])
            for c in cs:
                p=c['conditions'];tx=c['jobs'][1]
                self.assertEqual(len(c['jobs']),2)
                self.assertEqual(tx['physical_role'],p['link_tx_role'])
                self.assertEqual(tx['logical_node'],2)
                self.assertEqual(tx['serial'],m['boards'][tx['physical_role']]['serial'])
                self.assertEqual(set(c['inactive_tx_roles']),set(m['cir_link_tx_roles'])-{tx['physical_role']})
                self.assertEqual(tx['argv'][tx['argv'].index('--serial')+1],tx['serial'])

    def test_old_manifest_ids_and_single_n4_remain_compatible(self):
        m=frozen();m.pop('cir_link_tx_roles')
        # Reconstruct the pre-profile manifest shape as well.  Current
        # essential/lite filters intentionally name all six physical links,
        # whereas old manifests had only the implicit N4 link.
        m['paper']=m.pop('profiles')['full']
        for stage,count in [('exp2',24),('exp5',3)]:
            cs=manifest.plan(m,stage,profile='paper');self.assertEqual(len(cs),count)
            self.assertTrue(all('_txN' not in c['id'] and 'link_tx_role' not in c['conditions'] for c in cs))
            self.assertTrue(all(c['jobs'][1]['physical_role']=='N4' for c in cs))

    def test_manifest_rejects_duplicate_or_unknown_links(self):
        for links in [[],['N2','N2'],['init'],['N8'],'N4']:
            with tempfile.TemporaryDirectory() as td:
                m=frozen();m['cir_link_tx_roles']=links;p=Path(td)/'m.json';p.write_text(json.dumps(m))
                with self.assertRaises(ValueError):manifest.load(p)

    def test_unselected_lead_and_ambiguous_serial_rejected(self):
        m=manifest.load(API/'brrs_vehicle_manifest.json')
        for stage in ['exp2','exp5']:
            with self.assertRaises(ValueError):manifest.plan(m,stage)
            with self.assertRaises(ValueError):manifest.serial_for(m,stage,'tx')
            self.assertEqual(manifest.serial_for(m,stage,'tx','N7'),'1050204212')
        self.assertEqual(manifest.serial_for(m,'exp1','tx'),'1050282818')

    def test_real_cir_logs_relabel_by_verified_physical_link(self):
        m=frozen()
        for stage in ['exp2','exp5']:
            for role in ['N2','N7']:
                with self.subTest(stage=stage,role=role),tempfile.TemporaryDirectory() as td:
                    root=Path(td);c=selected(m,stage,role)
                    fake_bundle(root,m,c,real_raw(stage))
                    r=results.assess(root);serial=m['boards'][role]['serial']
                    self.assertEqual(r['verdict'],'PASS')
                    self.assertEqual(set(r['nodes_by_serial']),{serial})
                    self.assertEqual(r['stage_metrics']['physical_link']['tx_role'],role)
                    self.assertEqual(r['stage_metrics']['physical_link']['raw_node_id'],'N2')
                    # Wrong physical metadata must not become another valid link.
                    p=root/'results/remote'/f'{role}.meta.txt'
                    p.write_text(p.read_text().replace('physical_role='+role,'physical_role=N4'))
                    with self.assertRaises(ValueError):results.assess(root)

    def test_missing_link_does_not_disappear_in_aggregation(self):
        m=frozen();cases=manifest.plan(m,'exp5',profile='paper')
        observations={}
        for c in cases:
            role=c['conditions']['link_tx_role']
            if role=='N7':continue
            serial=m['boards'][role]['serial']
            observations[c['id']]={'case_id':c['id'],'verdict':'PASS','firmware_source_sha256':{},
                'nodes_by_serial':{serial:{'physical_role':role,'location':m['boards'][role]['location'],
                    'logical_node':2,'offered':1000,'rx':1000,'tx_success':1000,'per_percent':0}}}
        groups=results.aggregate(cases,observations)
        self.assertEqual(len(groups),6)
        missing=[g for g in groups if g['status']=='INCOMPLETE']
        self.assertEqual(len(missing),1);self.assertIn('_txN7',missing[0]['condition_id'])

    def test_single_host_export_validates_same_standard_evidence(self):
        for host in ['local','s-macbook-air']:
            with tempfile.TemporaryDirectory() as td:
                m=frozen()
                for b in m['boards'].values():b['host']=host
                root=Path(td);c=selected(m,'exp5','N7');fake_bundle(root,m,c,real_raw('exp5'))
                c=json.loads((root/'case.json').read_text())
                # Fixtures start as two logical output sides. Move input files to
                # independent collector paths, then export real-shaped proofs.
                state={'status':'COLLECTION_AND_READBACK_PASS','workers':{},'recovery':{},'halt_errors':{},
                    'payload_index_sha256':case.sha(root/'payload_hashes.json'),
                    'all_tx_ready_at':'2026-09-07T09:00:00+00:00','started_at':'2026-09-07T08:59:59+00:00','finished_at':'2026-09-07T09:01:00+00:00'}
                import shutil
                for j in c['jobs']:
                    role=j['physical_role'];side='local' if role=='init' else 'remote'
                    paths={}
                    for kind,suffix in [('raw','.log'),('meta','.meta.txt')]:
                        p=root/(role+suffix);shutil.copy2(root/'results'/side/(role+suffix),p);paths[kind]=str(p)
                    state['workers'][role]={'serial':j['serial'],'exit_code':0,'paths':paths,'raw_sha256':case.sha(paths['raw']),
                        'started_at':'2026-09-07T09:00:01+00:00'}
                    state['recovery'][role]={'halted':True,'readback':{'serial':j['serial'],'hex_sha256':j['hex_sha256'],'status':'PASS'}}
                for side in ['local','remote']:shutil.rmtree(root/'results'/side)
                single.export_cir_evidence(root,c,state)
                self.assertEqual(results.assess(root)['verdict'],'PASS')
                out=root/'results'/('local' if host=='local' else 'remote')/'status.json'
                bad=json.loads(out.read_text());bad['inactive_halted_after']['N5']=False;out.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):results.assess(root)

    def test_single_host_refuses_multiple_cir_transmitters(self):
        m=frozen();c=selected(m,'exp2','N2')
        for b in m['boards'].values():b['host']='local'
        c['boards']=m['boards']
        for j in c['jobs']:
            j.update(host='local',args=j['argv'][2:]+['--no-build'])
        self.assertEqual(len(single.layout(c)[0]),1)
        c['jobs'].append(copy.deepcopy(c['jobs'][1]))
        with self.assertRaises(ValueError):single.layout(c)

    def test_single_host_complete_cir_flow_and_failed_inactive_halt(self):
        # Exercise real run/export/assessment with synthetic control callbacks.
        # No J-Link/SSH/flash calls are allowed in this offline test.
        for stage in ['stage0','exp1','exp2','exp3','exp5']:
            for halt_failed in [False,True]:
                with self.subTest(stage=stage,halt_failed=halt_failed),tempfile.TemporaryDirectory() as td:
                    root=Path(td).resolve();m=frozen()
                    for b in m['boards'].values():b['host']='local'
                    c=selected(m,stage,'N7') if stage in ['exp2','exp5'] else manifest.plan(m,stage,profile='lite')[0]
                    raw=real_raw(stage) if stage in ['exp2','exp5'] else synthetic_single_link_raw(c['conditions'])
                    fake_bundle(root,m,c,raw)
                    c=json.loads((root/'case.json').read_text());paths={}
                    for j in c['jobs']:
                        role=j['physical_role'];side='local' if role=='init' else 'remote'
                        paths[role]={}
                        for kind,suffix in [('raw','.log'),('meta','.meta.txt')]:
                            p=root/(role+suffix);shutil.copy2(root/'results'/side/(role+suffix),p)
                            paths[role][kind]=str(p)
                    shutil.rmtree(root/'results')
                    recovery={j['physical_role']:{'halted':True,'readback':{'status':'PASS','serial':j['serial'],'hex_sha256':j['hex_sha256']}} for j in c['jobs']}
                    def fake_supervise(r,config,state,spawn):
                        state.update(all_tx_ready_at='2026-09-07T09:00:00+00:00',rf_runs_started=1)
                        for j in config['jobs']:
                            role=j['physical_role'];console=r/'results'/(role+'.console.log')
                            console.write_text('\n'.join('[done] '+k+'='+v for k,v in paths[role].items()))
                            state['workers'][role]={'serial':j['serial'],'exit_code':0,'console':str(console),'started_at':'2026-09-07T09:00:01+00:00'}
                    with patch.object(single,'preflight',return_value={'TEST_ONLY':True}), \
                         patch.object(single,'halt') as halt,patch.object(single,'supervise',side_effect=fake_supervise), \
                         patch.object(single,'park_all',return_value={'N5':'TEST_ONLY missing halt'} if halt_failed else {}), \
                         patch.object(single,'snapshot',return_value=recovery),patch.object(single.signal,'signal'), \
                         patch.object(single.subprocess,'Popen',side_effect=AssertionError('no hardware process')), \
                         patch('sys.stdout',new_callable=io.StringIO):
                        rc=single.run(root,c,case.sha(root/'payload_hashes.json'))
                    halt.assert_called_once_with(c,list(c['boards']))
                    summary=json.loads((root/'results/SUMMARY.json').read_text())
                    self.assertEqual(rc,1 if halt_failed else 0,(root/'results/status.json').read_text())
                    self.assertEqual(summary['verdict'],'INVALID' if halt_failed else 'PASS')
                    self.assertEqual(summary['rf_runs_started'],1)
                    if halt_failed:self.assertFalse((root/'results/ASSESSMENT.json').exists())
                    elif stage in ['exp2','exp5']:self.assertEqual(summary['stage_metrics']['physical_link']['tx_role'],'N7')
                    else:self.assertEqual(set(summary['nodes_by_serial']),{m['boards']['N4']['serial']})

    def test_dispatch_local_and_remote_failure_without_retry(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);(root/'payload_hashes.json').write_text('{}')
            m=frozen();c=selected(m,'exp5','N7');c['boards']=m['boards']
            for b in c['boards'].values():b['host']='local'
            a=SimpleNamespace(bundle=root,host=None)
            with patch.object(single,'run',return_value=0) as run, \
                 patch.object(case.subprocess,'run',side_effect=AssertionError('no network')):
                self.assertEqual(case.run_single_host(a,c),0);run.assert_called_once()
            for b in c['boards'].values():b['host']='s-macbook-air'
            with patch.object(case.subprocess,'run',side_effect=[SimpleNamespace(returncode=2),SimpleNamespace(returncode=0)]) as run:
                self.assertEqual(case.run_single_host(a,c),2)
            self.assertEqual(run.call_count,2)
            self.assertEqual([x.args[0][0] for x in run.call_args_list],['ssh','scp'])
            self.assertIn('--expected-index',run.call_args_list[0].args[0][-1])

    def test_legacy_runner_requires_explicit_link_and_emits_correct_serial(self):
        for stage in ['exp2','exp5']:
            cmd=['bash',str(API/'brrs_run_experiment.sh'),stage,'tx','TEST_ONLY_links','--dry-run']
            r=subprocess.run(cmd,text=True,capture_output=True)
            self.assertNotEqual(r.returncode,0)
            self.assertIn('select --physical-tx-role',r.stderr)
            r=subprocess.run(cmd+['--physical-tx-role','N7'],text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('--serial 1050204212',r.stdout)
            self.assertNotIn('--serial 1050282818',r.stdout)

    def test_campaign_dry_plan_has_no_processes(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'m.json';p.write_text(json.dumps(frozen()))
            args=type('Args',(),dict(manifest=p,stage='exp5',profile='preparation',confirmation=False,capacity_candidates=False,blocks=None,cases=None,dry_run=True))()
            with patch.object(subprocess,'run',side_effect=AssertionError('no hardware/process')),patch('sys.stdout',new_callable=io.StringIO) as out:
                campaign.make_campaign(args)
            d=json.loads(out.getvalue());self.assertEqual(len(d['case_ids']),6);self.assertFalse(d['rf_execution_performed'])

    def test_full_block_slice_is_not_a_new_profile(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/'m.json';p.write_text(json.dumps(frozen()))
            base=dict(manifest=p,stage='exp4',profile='full',confirmation=False,
                      capacity_candidates=False,cases=None,dry_run=True)
            for profile,count in [('full',58),('essential',8),('lite',8)]:
                args=type('Args',(),{**base,'profile':profile,'blocks':[1]})()
                with patch.object(subprocess,'run',side_effect=AssertionError('no hardware/process')),patch('sys.stdout',new_callable=io.StringIO) as out:
                    campaign.make_campaign(args)
                data=json.loads(out.getvalue())
                self.assertEqual(data['profile'],profile)
                self.assertEqual(data['selected_case_count'],count)
                self.assertTrue(all(case_id.startswith(profile+'_') and case_id.endswith('_b01')
                                    for case_id in data['case_ids']))
                self.assertEqual(data['profile_slice'],profile!='lite')
            args=type('Args',(),{**base,'blocks':[13]})()
            with self.assertRaises(ValueError):campaign.make_campaign(args)

if __name__=='__main__':unittest.main(verbosity=2)
