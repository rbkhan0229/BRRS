#!/usr/bin/env python3
"""Isolated, single-run Exp1 link diagnosis; never a frozen campaign result.

The existing capture verifier, immutable evidence reader and single-host runner
are reused. The campaign planner is neither invoked nor altered. Each fresh
manifest permits one run on physical N6 or N7 acting as logical N2.
"""
import argparse
import copy
import fcntl
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
API = HERE / 'sdk/Drivers/API'
sys.path.insert(0, str(API))
import vehicle_diagnostic as vehicle
from vehicle_diagnostic import SERIALS, bridge, new, unused, now, sha
from brrs_suite_case import prepare_resolved
from brrs_suite_paper import digest
from brrs_suite_evidence import read_evidence
from brrs_exp1_verify import validate_rx, validate_tx
from brrs_suite_results import marker
from brrs_wilson_ci import wilson_interval
import brrs_single_host as single

VERSION = 'isolated-link-diagnostic-v1'
PROFILE = 'causal_link_diagnostic'
PURPOSE = 'New vehicle cause investigation; excluded from Standard and Stage0'
LEAD = dict(frozen=False, provisional=True, lead_us=27,
            label='차량 미검증 임시 진단값',
            source='User-authorized diagnostic lead 27 us; historical Lite provenance retained separately')
AUTHORIZATION = dict(max_rf_runs=1, standard_inclusion=False, stage0_completion=False,
                     fixed_physical_mapping=True, selected_tx_logical_node=2,
                     inactive_boards_halted=True)


def conditions(tx_role, rx_mode):
    if tx_role not in ('N6', 'N7') or rx_mode not in ('delayed', 'immediate'):
        raise ValueError('Exp1 diagnosis supports only N6/N7 delayed/immediate')
    return dict(stage='exp1', preamble=32, rx_pac=8, beacon_preamble_symbols=512,
                lead_us=27, tail_us=0, cycles=2000, variant=None, slot_owners='2',
                run=1, sensors=1, rx_mode=rx_mode, profile=PROFILE,
                diagnostic_tx_role=tx_role, period_us=10000,
                uwb_channel=9, uwb_channel_evidence='source default and prepared ELF; no Exp1 runtime RF-register marker')


def manifest(cid, tx_role, rx_mode='delayed', environment=None):
    if not re.fullmatch(r'link_diag_[A-Za-z0-9_]+', cid or ''):
        raise ValueError('unique case ID must begin link_diag_')
    if environment is None or not isinstance(environment, dict) or not environment:
        raise ValueError('explicit environment observation/provenance object required')
    return dict(schema_version=VERSION, case_id=cid, environment=cid,
                purpose=PURPOSE, conditions=conditions(tx_role, rx_mode),
                boards={r:dict(serial=s, host='local',
                         location='vehicle_installed_position_not_reported') for r,s in SERIALS.items()},
                lead_selection=copy.deepcopy(LEAD), authorization=copy.deepcopy(AUTHORIZATION),
                vehicle=copy.deepcopy(environment))


def resolve(m, capture_script_sha256=None):
    p=m['conditions']; reference=manifest(m['case_id'], p['diagnostic_tx_role'], p['rx_mode'], m['vehicle'])
    if m != reference:
        raise ValueError('manifest differs from independent one-link diagnostic schema')
    ch=digest(p); mh=digest(m); jobs=[]
    # The runner itself always launches the TX before INIT, independent of list order.
    for role, logical, capture_role in [('init',1,'rx'),(p['diagnostic_tx_role'],2,'tx')]:
        args=[capture_role,'32','1',m['environment'],'--lead','27','--pac','8',
              '--rx-mode',p['rx_mode'],'--beacon-preamble','512','--serial',SERIALS[role]]
        env=dict(BRRS_SUITE_MANIFEST_SHA256=mh, BRRS_SUITE_CONDITIONS_SHA256=ch,
                 BRRS_SUITE_CASE_ID=m['case_id'], BRRS_SUITE_PHYSICAL_ROLE=role,
                 BRRS_SUITE_LOGICAL_NODE=str(logical), BRRS_SUITE_PROFILE=PROFILE,
                 BRRS_RECONNECT_ATTEMPTS='0', BRRS_STRICT_CONNECTION='1')
        argv=['bash',str(API/'brrs_exp1_capture.sh'),*args]
        jobs.append(dict(physical_role=role, logical_node=logical, serial=SERIALS[role],
                         host='local', location=m['boards'][role]['location'],
                         argv=argv, build_only_argv=argv+['--build-only'], environment=env,
                         source_script_sha256=capture_script_sha256 or sha(API/'brrs_exp1_capture.sh')))
    return dict(id=m['case_id'], conditions=copy.deepcopy(p), conditions_sha256=ch,
                jobs=jobs, per_node_goal_strictly_below=1.0,
                inactive_tx_roles=[r for r in SERIALS if r not in ('init',p['diagnostic_tx_role'])],
                diagnostic_only=True)


def validate_bundle(bundle, index):
    bundle=Path(bundle).resolve(); c=single.load_bundle(bundle,index)
    m=json.loads((bundle/'board_manifest.json').read_text())
    expected=resolve(m,sha(bundle/'sdk/Drivers/API/brrs_exp1_capture.sh'))
    for key in ('id','conditions','conditions_sha256','inactive_tx_roles','diagnostic_only','per_node_goal_strictly_below'):
        if c.get(key)!=expected[key]: raise ValueError('case binding: '+key)
    if c['boards']!=m['boards'] or c['manifest_file_sha256']!=sha(bundle/'board_manifest.json'):
        raise ValueError('manifest/board binding')
    if len(c['jobs'])!=2: raise ValueError('exactly INIT + selected TX required')
    for job,ref in zip(c['jobs'],expected['jobs']):
        for key in ('physical_role','logical_node','serial','host','location','environment','source_script_sha256'):
            if job[key]!=ref[key]: raise ValueError('job binding: '+key)
        if job['args']!=ref['argv'][2:]+['--no-build','--timeout','180'] or job['script']!='brrs_exp1_capture.sh':
            raise ValueError('capture arguments mismatch')
        config='Exp1_32_Init' if job['logical_node']==1 else 'Exp1_Normal'
        if job['hex']!=f'sdk/Drivers/API/Build_Platforms/nRF52840-DK/Output/{config}/Exe/dw3000_api.hex':
            raise ValueError('unexpected Exp1 image path')
    return c,expected


ERROR_RE=re.compile(r'^RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+)\)  delayed schedule late=(\d+)  data config errors=(\d+)$')
ERROR_NAMES=('timeouts','fwto','pto','errors','sfdto','phe','fce','fsl','delayed_late','data_config_errors')
CAUSES=('fwto','pto','sfdto','phe','fce','fsl','other')
SUMMARY_KEYS={'events','diag_ok','valid','invalid_no_rxprd','invalid_zero','invalid_range','read_fail','hist_overflow'}


def integers(fields):
    result={}
    for item in fields:
        key,sep,value=item.partition('=')
        if not sep or key in result or not re.fullmatch(r'\d+',value):
            raise ValueError('malformed/duplicate diagnostic field')
        result[key]=int(value)
    return result


def parse_error_accum(text, *, preamble=32):
    """Account for every failure event, preserving invalid/stale diagnostics."""
    if type(preamble) is not int or not 1<=preamble<=4096:
        raise ValueError('invalid ACCUM preamble bound')
    lines=text.splitlines(); matches=[ERROR_RE.fullmatch(x) for x in lines if x.startswith('RX timeouts=')]
    if len(matches)!=1 or matches[0] is None: raise ValueError('missing/duplicate/malformed RX error counters')
    counters=dict(zip(ERROR_NAMES,map(int,matches[0].groups())))
    if counters['delayed_late'] or counters['data_config_errors']:
        raise ValueError('schedule/configuration failure')
    notes=[l for l in lines if l.startswith('FAIL_ACCUM_NOTE,')]
    if notes!=['FAIL_ACCUM_NOTE,valid_requires=RXPRD_and_1_to_PLEN,invalid_raw_may_be_stale']:
        raise ValueError('failure ACCUM validity declaration missing/duplicate')
    summaries={};histograms={}; all_diagnostic_lines=[]
    for line in lines:
        if line.startswith('FAIL_ACCUM_SUMMARY_CSV,'):
            row=line.split(','); cause=row[2] if len(row)>2 else None
            if len(row)<4 or row[1]!='N2' or cause not in CAUSES or cause in summaries:
                raise ValueError('failure summary identity/duplication')
            v=integers(row[3:])
            if set(v)!=SUMMARY_KEYS or v['events']<=0:
                raise ValueError('failure summary schema/count')
            if v['diag_ok']+v['read_fail']!=v['events'] or v['valid']+sum(v[k] for k in ('invalid_no_rxprd','invalid_zero','invalid_range'))!=v['diag_ok']:
                raise ValueError('failure validity totals inconsistent')
            summaries[cause]=v;all_diagnostic_lines.append(line)
        elif line.startswith('FAIL_ACCUM_HIST_CSV,'):
            row=line.split(',')
            if len(row)!=6 or row[1]!='N2' or row[2] not in CAUSES or row[3] not in ('valid','invalid_raw'):
                raise ValueError('failure histogram identity/schema')
            v=integers(row[4:])
            if set(v)!={'accum','n'} or not 0<=v['accum']<=65535 or v['n']<=0:
                raise ValueError('invalid histogram values')
            if row[3]=='valid' and not 1<=v['accum']<=preamble: raise ValueError('valid ACCUM outside PHY range')
            bins=histograms.setdefault(row[2],{}).setdefault(row[3],{})
            if v['accum'] in bins: raise ValueError('duplicate histogram bin')
            bins[v['accum']]=v['n'];all_diagnostic_lines.append(line)
    if set(histograms)-set(summaries): raise ValueError('histogram without summary')
    for cause,v in summaries.items():
        h=histograms.get(cause,{}); good=sum(h.get('valid',{}).values()); bad=sum(h.get('invalid_raw',{}).values())
        if good>v['valid'] or bad>v['diag_ok']-v['valid'] or good+bad+v['hist_overflow']!=v['diag_ok']:
            raise ValueError('histogram totals/overflow mismatch')
        if v['read_fail']: raise ValueError('CIA diagnostic read failure')
    events=sum(v['events'] for v in summaries.values())
    if events!=counters['timeouts']+counters['errors']: raise ValueError('failure events do not match RX outcomes')
    successful={}
    for line in lines:
        match=re.fullmatch(r'accum=(\d+): n=(\d+)',line)
        if match:
            accum,n=map(int,match.groups())
            if accum>preamble or n<=0 or accum in successful: raise ValueError('successful ACCUM histogram malformed')
            successful[accum]=n
    return dict(status='PASS', error_counters=counters, failure_events=events,
                failure_summaries=summaries, failure_histograms=histograms,
                successful_accum_histogram=successful, successful_accum_samples=sum(successful.values()),
                totals={key:sum(v[key] for v in summaries.values()) for key in SUMMARY_KEYS},
                failure_classification_scope='One priority class per RX error/timeout; raw status bit counters can overlap',
                validity_scope='Firmware RXPRD and ACCUM range heuristic; invalid_raw can be stale, desired-frame association not proven',
                raw_error_cir_captured=False, absent_causes='No summary rows emitted for zero-event causes; missing measurement values are not imputed',
                diagnostic_lines=all_diagnostic_lines)


def assess_diagnostic(bundle):
    bundle=Path(bundle).resolve(); c,expected=validate_bundle(bundle,sha(bundle/'payload_hashes.json'))
    state=json.loads((bundle/'results/status.json').read_text());p=c['conditions'];role=p['diagnostic_tx_role']
    if state['status']!='COLLECTION_AND_READBACK_PASS' or state['rf_runs_started']!=1 or state.get('halt_errors'):
        raise ValueError('exactly one fully collected and parked RF required')
    if set(state['workers'])!={'init',role} or set(state['recovery'])!={'init',role}:
        raise ValueError('active worker/recovery set differs')
    if not state['workers'][role]['ready_at']<=state['all_tx_ready_at']<=state['workers']['init']['started_at']:
        raise ValueError('INIT started before selected TX READY')
    for job in c['jobs']:
        recovery=state['recovery'][job['physical_role']]
        if not recovery.get('halted') or recovery['readback']['status']!='PASS':raise ValueError('active readback/halt failure')
    c,raw,orch=read_evidence(bundle,expected)
    if orch['case_id']!=c['id']:raise ValueError('orchestration case identity')
    args=SimpleNamespace(preamble=32,lead=27,tail=0,pac=8,rx_mode=p['rx_mode'],expected=2000,beacon_preamble=512)
    rx='\n'.join(raw['init']);tx='\n'.join(raw[role])
    verify_rx=validate_rx(rx,args);verify_tx=validate_tx(tx,args)
    rv=marker(raw['init'],'EXP1_DONE,');tv=marker(raw[role],'EXP1_TX_DONE,')
    received=int(rv['rx']);sent=int(tv['success']);offered=2000
    if not 0<=received<=sent<=offered:raise ValueError('inconsistent valid RX or TX count')
    for job in c['jobs']:
        role_name=job['physical_role'];path=bundle/'results/local'/(role_name+'.meta.txt')
        meta=dict(l.split('=',1) for l in path.read_text().splitlines() if '=' in l)
        expected_meta=dict(suite_profile=PROFILE,mode='exp1',role='rx' if role_name=='init' else 'tx',
            preamble_symbols='32',beacon_preamble_symbols='512',lead_us='27',tail_us='0',pac='8',
            rx_mode=p['rx_mode'],expected_cycles='2000',run_number='1',environment=c['id'],capture_method='pylink',status='PASS')
        if any(meta.get(k)!=v for k,v in expected_meta.items()):raise ValueError('Exp1 metadata parameters: '+role_name)
    diagnostic=parse_error_accum(rx)
    diagnostic.update(case_id=c['id'],physical_tx_role=role,physical_tx_serial=SERIALS[role],
                      logical_tx_node=2, offered=offered,tx_success=sent,rx=received,
                      unsent=offered-sent,post_tx_loss=sent-received)
    # JSON object keys are strings; normalize histogram bin keys for stable
    # read-only re-assessment of the immutable sidecar on the other host.
    diagnostic=json.loads(json.dumps(diagnostic))
    sidecar=bundle/'results/LINK_ERROR_ACCUM.json'
    if sidecar.exists():
        if json.loads(sidecar.read_text())!=diagnostic:raise ValueError('existing diagnostic sidecar differs')
    else:new(sidecar,diagnostic)
    if received==0:raise ValueError('zero valid packets; raw and error/ACCUM sidecar preserved')
    per=100*(offered-received)/offered;verdict='PASS' if (offered-received)*100<offered else 'FAIL_PER'
    row=dict(physical_role=role,logical_node=2,location=c['boards'][role]['location'],offered=offered,
             tx_attempts=int(tv['attempts']),tx_success=sent,rx=received,per_percent=per,
             per_wilson95_percent=[100*x for x in wilson_interval(offered-received,offered,.95)])
    return dict(case_id=c['id'],conditions=p,bundle=str(bundle),verdict=verdict,worst_node_per_percent=per,
                nodes_by_serial={SERIALS[role]:row},measurement_status='VALID',stage_metrics=dict(
                    physical_link=dict(tx_role=role,tx_serial=SERIALS[role],rx_serial=SERIALS['init'],raw_node_id='N2'),
                    unsent=offered-sent,post_tx_loss=sent-received,error_accum_sidecar='LINK_ERROR_ACCUM.json'),
                error_counters=diagnostic['error_counters'],existing_verifier=dict(rx=verify_rx,tx=verify_tx),
                per_criterion='Strict PER < 1%; same integer criterion as brrs_suite_results.assess',
                assessment_scope='Independent provisional-link diagnosis using unchanged official capture/evidence verifiers; not a campaign assessment',
                scope=PURPOSE,lead_frozen=False,provisional_lead_us=27,stage0_complete=False,standard_inclusion=False,
                rf_runs_started=1,diagnostic_policy=VERSION,controller_policy=bridge.POLICY_VERSION,
                operational_status=bridge.policy('exp1',verdict,per,[]),
                payload_index_sha256=sha(bundle/'payload_hashes.json'),firmware_source_sha256=c['firmware_source_sha256'],
                source_git=c.get('source_git'),log_sha256_by_serial={j['serial']:sha(bundle/'results/local'/(j['physical_role']+'.log')) for j in c['jobs']})


def prepare(root,cid,tx_role,rx_mode,environment,hardware_preflight=True):
    root=Path(root).resolve();root.mkdir(parents=True,exist_ok=False)
    try:
        m=manifest(cid,tx_role,rx_mode,environment);new(root/'diagnostic-manifest.json',m)
        if hardware_preflight:new(root/'preflight.json',vehicle.remote_check())
        # Rebuild both images serially: the existing cache stamps do not bind
        # source hashes, so they are not sufficient provenance for this path.
        a=SimpleNamespace(manifest=root/'diagnostic-manifest.json',bundle=root/'bundle',reuse=False,source_build=True)
        prepare_resolved(a,m,resolve(m));bundle=a.bundle
        for source in (Path(__file__),Path(vehicle.__file__),Path(bridge.__file__)):
            shutil.copy2(source,bundle/source.name)
        new(bundle/'provenance/link-diagnostic.json',dict(version=VERSION,controller_policy=bridge.POLICY_VERSION,
            controller_sha256=sha(__file__),source_api=str(API),rf_started=False,
            stage0_frozen=False,standard_inclusion=False,original_campaign_functions_unchanged=True,
            toolchain_sha256={str(Path(os.environ[k])):sha(os.environ[k]) for k in ('EMBUILD','ARM_NM')}))
        hashes={str(p.relative_to(bundle)):sha(p) for p in sorted(bundle.rglob('*')) if p.is_file() and p.name!='payload_hashes.json'}
        (bundle/'payload_hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
        index=sha(bundle/'payload_hashes.json');validate_bundle(bundle,index)
        rb=bridge.REMOTE/'causal_link_diagnostics'/cid/'bundle'
        new(root/'prepared.json',dict(case_id=cid,bundle=str(bundle),remote_bundle=str(rb),payload_index_sha256=index,
            manifest_sha256=sha(root/'diagnostic-manifest.json'),hardware_preflight_completed=hardware_preflight,
            rf_started=False,policy_version=bridge.POLICY_VERSION))
    except BaseException as exc:
        (root/'STOP').touch();new(root/'prepare-failure.json',dict(at=now(),error=repr(exc),rf_started=False));raise


def local_run(bundle,index):
    bundle=Path(bundle).resolve();c,_=validate_bundle(bundle,index);unused(bundle)
    if (bridge.REMOTE/'STOP_ALL').exists():raise ValueError('Air STOP_ALL')
    new(bundle/'local-started.json',dict(at=now(),index=index,case_id=c['id'],rf_runs_limit=1))
    try:
        rc=single.run(bundle,c,index,assessor=assess_diagnostic)
        new(bundle/'local-finished.json',dict(at=now(),exit_code=rc));return rc
    finally:(bundle/'STOP').touch()


def execute(root):
    root=Path(root).resolve()
    with (vehicle.WORK/'air-controller.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);unused(root)
        spec=json.loads((root/'prepared.json').read_text());bundle=Path(spec['bundle']);index=spec['payload_index_sha256']
        if bundle.resolve()!=root/'bundle' or sha(root/'diagnostic-manifest.json')!=spec['manifest_sha256']:
            raise ValueError('prepared root/manifest binding')
        c,_=validate_bundle(bundle,index);unused(bundle)
        rb=bridge.REMOTE/'causal_link_diagnostics'/c['id']/'bundle'
        if spec['case_id']!=c['id'] or spec['remote_bundle']!=str(rb):raise ValueError('remote case binding')
        new(root/'started.json',dict(at=now(),case_id=c['id'],payload_index_sha256=index,
            controller_sha256=sha(__file__),policy_version=bridge.POLICY_VERSION,rf_runs_limit=1))
        new(root/'active-air-case.json',dict(case_id=c['id'],remote_bundle=str(rb)))
        try:
            new(root/'pre-run-check.json',vehicle.remote_check())
            bridge.remote_python("from pathlib import Path\nimport sys,json\nb=Path(sys.argv[1]);base=Path(sys.argv[2])\nif (base/'STOP_ALL').exists():raise RuntimeError('Air STOP_ALL')\nb.parent.mkdir(parents=True,exist_ok=False)\n(base/'ACTIVE.json').write_text(json.dumps({'bundle':str(b)}))\n",rb,bridge.REMOTE)
            subprocess.run(['scp','-q','-r',str(bundle),'brrs-air:'+str(rb)],check=True,timeout=180)
            cmd=['/usr/bin/caffeinate','-i','env','ARM_NM='+bridge.NM,'PYTHONDONTWRITEBYTECODE=1',
                 '/usr/bin/python3',str(rb/'link_diagnostic.py'),'local-run','--root',str(rb),'--index',index]
            if (root/'STOP').exists() or (root.parent/'STOP').exists():raise ValueError('local case/group STOP before RF')
            with (root/'air-console.log').open('x') as log:
                rc=subprocess.run(bridge.SSH+[shlex.join(cmd)],stdout=log,stderr=subprocess.STDOUT).returncode
            copies={}
            for item in ('results','logs','local-started.json','local-finished.json','STOP'):
                copies[item]=subprocess.run(['scp','-q','-r','brrs-air:'+str(rb/item),str(bundle)],timeout=180).returncode
            new(root/'transfer.json',dict(remote_exit=rc,copies=copies))
            if rc or any(copies.values()):raise RuntimeError('collection/control/copy failure')
            assessment=assess_diagnostic(bundle);new(root/'DIAGNOSTIC_ASSESSMENT.json',assessment)
            new(root/'finished.json',dict(at=now(),status='ONE_DIAGNOSIS_FINISHED',rf_runs_started=1,
                verdict=assessment['verdict'],operational_status=assessment['operational_status']))
        except BaseException as exc:
            new(root/'failure.json',dict(at=now(),error=repr(exc),no_retry=True));raise
        finally:
            # Keep the bridge's STOP propagation and preserve every case boundary.
            (root/'STOP').touch()
            try:bridge.stop(root)
            except Exception as exc:new(root/'stop-delivery-error.json',dict(at=now(),error=repr(exc)))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command',choices=('prepare','build-only','check','run','local-run','assess'))
    ap.add_argument('--root',type=Path,required=True);ap.add_argument('--case-id')
    ap.add_argument('--tx-role',choices=('N6','N7'));ap.add_argument('--rx-mode',choices=('delayed','immediate'),default='delayed')
    ap.add_argument('--environment-json',type=Path);ap.add_argument('--index')
    a=ap.parse_args();root=a.root.resolve()
    if a.command in ('prepare','build-only'):
        if not a.environment_json:ap.error('--environment-json required for preparation')
        prepare(root,a.case_id,a.tx_role,a.rx_mode,json.loads(a.environment_json.read_text()),a.command=='prepare')
    elif a.command=='check':
        spec=json.loads((root/'prepared.json').read_text())
        if Path(spec['bundle']).resolve()!=root/'bundle' or sha(root/'diagnostic-manifest.json')!=spec['manifest_sha256']:
            raise ValueError('prepared root/manifest binding')
        validate_bundle(Path(spec['bundle']),spec['payload_index_sha256']);unused(root)
        print(json.dumps(dict(spec,valid=True,rf_started=False)))
    elif a.command=='run':execute(root)
    elif a.command=='local-run':return local_run(root,a.index)
    else:print(json.dumps(assess_diagnostic(root),indent=2))
    return 0


if __name__=='__main__':
    try:sys.exit(main())
    except Exception as exc:print('FAIL: '+repr(exc),file=sys.stderr);sys.exit(1)
