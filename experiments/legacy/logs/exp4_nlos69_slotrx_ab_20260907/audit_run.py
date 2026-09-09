import argparse,datetime,hashlib,json,pathlib,subprocess,sys
import verify_base as v
root=pathlib.Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('variant',choices=['A','B']);ap.add_argument('run',type=int);a=ap.parse_args()
tag=f'{a.variant}_r{a.run}';folder=root/tag;m=json.loads((folder/'manifest.json').read_text());out={'tag':tag,'variant':a.variant,'run':a.run,'valid':False,'failures':[],'roles':m['variants'][a.variant]}
try:
    board_lines={}
    for side,roles in [('init',['init']),('tx',['N2','N3','N4'])]:
        state=json.loads((folder/side/'status.json').read_text());assert state['status']=='CAPTURE_COMPLETE_NOT_YET_RF_VERIFIED'
        assert state['manifest_sha256']==hashlib.sha256((folder/'manifest.json').read_bytes()).hexdigest()
        for role in roles:
            worker=state['workers'][role];rec=m['variants'][a.variant][role];raw=folder/side/(role+'.log');text=raw.read_text();lines=text.splitlines();board_lines[role]=lines
            assert worker['serial']==rec['serial'] and worker['sha256']==rec['sha256'] and worker['exit_code']==0
            assert worker['raw_sha256']==hashlib.sha256(raw.read_bytes()).hexdigest()
            assert text.count('===== END STATS =====')==1 and text.count('EXP_LOG_READY,channel=1')==1
            if role=='init':v.verify_init(lines,32,3,250,15,8)
            else:
                v.verify_sensor(lines,32,3,int(role[-1]),250)
                tx=v.last_line(lines,'EXP4_TX_RESULT_CSV,').split(',')
                assert list(map(int,tx[4:10]))==[1000,0,1000,1000,0,1],tx
        readback=json.loads((folder/(side+'_flash_readback.json')).read_text())
        for role in roles:assert readback['boards'][role]['status']=='PASS' and readback['boards'][role]['hex_sha256']==m['variants'][a.variant][role]['sha256']
    lines=board_lines['init'];records={}
    for line in lines:
        if '=' in line and ',' in line:records.setdefault(line.split(',')[0],[]).append(v.key_values(line))
    out['init_records']=records;out['nodes']={}
    for line in lines:
        if line.startswith('EXP4_NODE_CSV,'):
            f=line.split(',');offered,rx,lost,errors=map(int,f[3:7]);assert offered==1000 and rx+lost==offered
            out['nodes'][f[1]]={'offered':offered,'rx':rx,'lost':lost,'per_pct':100*lost/offered,'rx_error_attributed':errors}
    assert set(out['nodes'])=={'N2','N3','N4'}
    out['total']={k:sum(x[k] for x in out['nodes'].values()) for k in ['offered','rx','lost']};assert out['total']['rx']>0
    out['total']['per_pct']=100*out['total']['lost']/out['total']['offered']
    expected_mode='delayed_first_manual_double_buffer_burst' if a.variant=='A' else 'per_slot_delayed_bounded_single_attempt'
    assert records['EXP4_FIRMWARE_REV'][0]['data_rx']==expected_mode
    cfg=records['EXP4_CONFIG_CSV'][0]
    assert cfg['sync_buffer_us']=='3000' and cfg['sync_prep_deadline_us']=='7500'
    double=records['EXP4_DOUBLE_BUFFER_CSV'][0]
    for key in ['rdb_host_mismatch','rdb_incomplete','rdb_incomplete_recovered','rdb_resync','overrun']:
        if key in double:assert int(double[key])==0,(key,double[key])
    if a.variant=='B':
        windows=records.get('EXP4_SLOT_RX_CSV',[]);assert len(windows)==3,windows
        for w in windows:
            assert int(w['attempted'])==1000 and int(w['armed'])==1000 and int(w['late'])==0,w
            assert int(w['rx_good'])+int(w['timeout'])+int(w['error'])==1000,w
            assert int(w['rx_good'])==out['nodes']['N'+w['owner']]['rx'],w
            if int(w['slack_samples'])>0:assert int(w['min_arm_slack_us'])>0,w
            assert int(w['window_us'])==112 and int(w['fwto_uus'])==110,w
    out['tx_records']={role:[l for l in ls if l.startswith(('EXP4_TX','BRRS_NORMAL','EXP4_NORMAL','EXP4_SENSOR','BRRS_BEACON_RX','EXP4_FIRMWARE','EXP4_DONE','EXP4_SYNC_RX'))] for role,ls in board_lines.items() if role!='init'}
    out['valid']=True;out['goal_pass']=all(x['per_pct']<1 for x in out['nodes'].values());out['collection']='PASS';out['system']='PASS';out['goal']='PASS' if out['goal_pass'] else 'FAIL_PER'
except Exception as e:
    out['failures'].append(repr(e));out['goal']='INVALID'
(folder/'audit.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:val for k,val in out.items() if k not in ['roles','init_records','tx_records']},indent=2))
if not out['valid']:sys.exit(2)
