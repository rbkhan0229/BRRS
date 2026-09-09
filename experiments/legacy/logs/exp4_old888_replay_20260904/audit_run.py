"""Offline cross-version audit; never promotes high PER to PASS."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def module(path,name):
    spec=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
def require(test,message):
    if not test: raise ValueError(message)
def row(lines,prefix):
    found=[l for l in lines if l.startswith(prefix+',')]
    require(len(found)==1,f'{prefix}: expected one row, got {len(found)}')
    return dict(x.split('=',1) for x in found[0].split(',')[1:] if '=' in x)
def audit(variant,run):
    manifest=json.loads((ROOT/'manifest.json').read_text()); directory=ROOT/f'{variant}_r{run}'
    verifier_path=ROOT/'verify_old.py' if variant=='old' else Path(manifest['capture_script']).with_name('brrs_exp4_verify.py')
    v=module(verifier_path,'verifier')
    result={'variant':variant,'run':run,'system_failures':[], 'verified_roles':{},
        'verifier_sha256':digest(verifier_path),'manifest_sha256':digest(ROOT/'manifest.json'),
        'limitations':['Old firmware lacks new SPI fault counters: missing is not zero.',
        'Current diagnostics OFF: error subtype may be lost after rearm; not comparable with old subtype histograms.',
        'Current N3=888 matches historical role identity; physical placement/power equivalence is user-reported, not independently instrumented.']}
    logs={}
    def check(name,fn):
        try: return fn()
        except Exception as exc: result['system_failures'].append(f'{name}: {exc}'); return None
    for side,roles in [('init',['init']),('tx',['N2','N3','N4'])]:
        def load_side():
            state=json.loads((directory/side/'status.json').read_text())
            require(state['status']=='CAPTURE_COMPLETE_NOT_YET_RF_VERIFIED','capture did not complete')
            require(state['manifest_sha256']==result['manifest_sha256'],'manifest differs')
            require(state['variant']==variant and state['run']==run and state['side']==side,'wrong run identity')
            require(set(state['workers'])==set(roles),'wrong role set')
            for role in roles:
                worker=state['workers'][role]; record=manifest['variants'][variant][role]
                require(worker['serial']==record['serial'],'serial differs')
                require(worker['sha256']==record['sha256']==digest(ROOT/record['hex']),'firmware hash differs')
                raw=directory/side/f'{role}.log'
                require(worker['raw_sha256']==digest(raw),'raw hash differs')
                require(worker['exit_code']==0 and worker['ready'],'capture incomplete')
                lines=raw.read_text().splitlines(); logs[role]=lines
                require(lines.count('===== END STATS =====')==1,'END count mismatch')
        check(side+' capture',load_side)
    saved_fail=v.fail
    def fail_keep_per(message):
        if re.fullmatch(r'PER [0-9]+\.[0-9]{3}% > limit 5\.000%',message): return
        saved_fail(message)
    v.fail=fail_keep_per
    for role,lines in logs.items():
        def verify():
            if role=='init':
                if variant=='old': detail=v.verify_init(lines,32,3,250,15,8)
                else: detail=v.verify_init(lines,32,3,250,15,8,3000,2500,5.0,spi_opt=True,expected_cycles=1000)
                config=row(lines,'EXP4_CONFIG_CSV')
                require(config['sync_buffer_us']=='3000' and config['sync_prep_deadline_us']=='7500','wait budget mismatch')
                db=row(lines,'EXP4_DOUBLE_BUFFER_CSV')
                for key in ['rdb_host_mismatch','rdb_incomplete','rdb_incomplete_recovered','rdb_resync','overrun']:
                    require(db.get(key)=='0',f'{key}={db.get(key)}')
            else:
                if variant=='old': detail=v.verify_sensor(lines,32,3,int(role[1]),250)
                else: detail=v.verify_sensor(lines,32,3,int(role[1]),250,3000,2500,expected_cycles=1000)
                done=row(lines,'EXP4_TX_DONE')
                for key,value in {'beacons':'1000/1000','attempts':'1000','success':'1000','schedule':'PASS'}.items():
                    require(done.get(key)==value,f'{key} differs')
            result['verified_roles'][role]=detail
        check(role+' verification',verify)
    v.fail=saved_fail
    if 'init' in logs:
        def extract():
            lines=logs['init']; done=row(lines,'EXP4_DONE')
            expected=int(done['expected']); rx=int(done['rx'])
            require(expected==3000 and 0<rx<=expected,'invalid/zero RX')
            result.update(expected=expected,rx=rx,per_percent=100*(expected-rx)/expected,nodes={})
            for line in lines:
                if line.startswith('EXP4_NODE_CSV,'):
                    _,node,plen,exp,received,miss,err=line.split(',')
                    require(int(exp)==1000 and int(exp)-int(received)==int(miss),'node counts inconsistent')
                    result['nodes'][node]={'rx':int(received),'expected':int(exp),'per_percent':100*int(miss)/int(exp),'error_estimate':int(err)}
            require(set(result['nodes'])=={'N2','N3','N4'},'wrong node set')
            require(sum(n['rx'] for n in result['nodes'].values())==rx,'aggregate mismatch')
            result['rx_errors']=[l for l in lines if l.startswith('RX timeouts=')]
            result['system_rows']=[l for l in lines if l.startswith(('TDMA validation:','EXP4_REARM_CSV,','EXP4_DOUBLE_BUFFER_CSV,','EXP4_SPI_CSV,','EXP4_DEFERRED_CSV,','EXP4_STATUS_CSV,'))]
        check('aggregate',extract)
    if set(result['verified_roles'])!={'init','N2','N3','N4'}: result['system_failures'].append('not all roles verified')
    result['disposition']='FAIL_SYSTEM' if result['system_failures'] else ('FAIL_PER' if result['per_percent']>5 else 'PASS')
    output=directory/'audit.json'
    with output.open('x') as f: json.dump(result,f,indent=2)
    return result
if __name__=='__main__':
    ap=argparse.ArgumentParser(); ap.add_argument('--variant',choices=['old','current'],required=True); ap.add_argument('--run',type=int,required=True); a=ap.parse_args()
    print(json.dumps(audit(a.variant,a.run),indent=2))
