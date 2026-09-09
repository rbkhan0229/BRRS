import json,re,sys
from pathlib import Path
from datetime import datetime
R=Path(__file__).resolve().parent;B=R/'capture1'
sys.path.insert(0,str(B/'sdk/Drivers/API'))
from brrs_suite_case import checked,save
from brrs_suite_results import assess
c=checked(B);a=assess(B)
o=json.loads((B/'results/orchestration.json').read_text())
assert o['status']=='COLLECTION_AND_READBACK_PASS_PER_PENDING'
raw=(B/'results/local/init.log').read_text()
pat=r'RX timeouts=(\d+) \(fwto=(\d+) pto=(\d+)\)  RX errors=(\d+) \(sfdto=(\d+) phe=(\d+) fce=(\d+) fsl=(\d+) fint-only=(\d+) overrun=(\d+)\)'
match=re.search(pat,raw);assert match
errors=dict(zip(['rx_timeout','fwto','pto','rx_errors','sfd_timeout','phr_error','crc_error','rxfsl','fint_only','overrun'],map(int,match.groups())))
controls={}
for j in c['jobs']:
 role=j['physical_role'];side='local' if role=='init' else 'remote';p=B/'results'/side
 log=(p/(role+'.log')).read_text();console=(p/(role+'.console.log')).read_text()
 meta=dict(line.split('=',1) for line in (p/(role+'.meta.txt')).read_text().splitlines() if '=' in line)
 status=json.loads((p/'status.json').read_text())
 controls[role]={'serial':j['serial'],'ready_markers':log.splitlines().count('EXP_LOG_READY,channel=1'),
  'end_markers':log.splitlines().count('===== END STATS ====='),'collection_status':meta['collection_status'],
  'timeout':bool(re.search(r'^\[rtt_capture\].*(?:TIMEOUT|timed out)',console,re.M)),
  'readback':status['workers'][role]['readback']}
 assert controls[role]['ready_markers']==controls[role]['end_markers']==1
 assert controls[role]['collection_status']==controls[role]['readback']['status']=='PASS'
 assert not controls[role]['timeout']
classes=[dict(v.split('=',1) for v in line.split(',')[1:]) for line in raw.splitlines() if line.startswith('EXP4_SLOT_CLASS_CSV,')]
assert sum(int(x['count']) for x in classes)==a['aggregate']['rx']
wrongsrc=sum(int(x['count']) for x in classes if x['src']!=x['observed_owner'])
post={}
for side in ['local','remote']:
 pre=json.loads((R/f'preflight_{side}.json').read_text());after=json.loads((R/f'postflight_{side}.json').read_text())
 post[side]={'git_unchanged':pre['git']==after['git'],'probes_unchanged':pre['probes']==after['probes'],'capture_processes':after['capture_processes']}
 assert post[side]['git_unchanged'] and post[side]['probes_unchanged'] and not post[side]['capture_processes']
report={'valid_rf_runs':1,'assessment':a,'orchestration':o,'phy_errors':errors,'controls':controls,
 'wrong_source_observed_slot_count':wrongsrc,'postflight':post,'setup':json.loads((R/'manifest.json').read_text())['setup_record'],
 'counters_raw_lines':[line for line in raw.splitlines() if any(s in line for s in ['wrong','late','RDB','EXP4_DEFERRED','EXP4_SPI','EXP4_TIMING','RX timeouts='])]}
save(R/'RESULTS.json',report)
loc={'N2':'범퍼 A','N3':'범퍼 B','N4':'1열 운전석','N5':'1열 조수석','N6':'트렁크 A','N7':'트렁크 B'}
lines=['# 차량 글러브박스 RX · M32/PAC8 lead26 · TX6 단일 실행','',
 f"판정: **{a['verdict']}**, 최악 노드 PER **{a['worst_node_per_percent']:.4f}%**. 유효 RF 실행은 정확히 1회다.",'',
 f"제어·수집 시각: {o['started_at']} ~ {o['finished_at']} (UTC). DATA 측정은 1,000 superframe, 약10초.",'',
 'SSH s-macbook-air 정상, 로컬 RX1/원격 TX6의 실제 serial 집합 확인. RX는 현재 글러브박스다. TX는 기존 설치표 순서대로 범퍼2·1열2·트렁크2. 시동 켜짐, TX허브 외부 전원 연결, 모든 문 닫힘은 사용자 확인. 차종·창문·보닛·트렁크 개폐의 별도 확인은 미제공으로 기록했다. 물리 위치·방향·배선·전원을 변경하지 않았다.','',
 'M32/PAC8, lead26µs, G250, SB/SP3000/2500µs, SF10ms, S6/K13, 슬롯 순서2345672345673. 슬롯별 bounded delayed-RX, RX창123µs/FWTO120UUS, 기존 SPI 경로. 재전송 없이 각 슬롯은 별도 예정 송신 기회다. 원래 역할을 유지했으며 N3가SF당3회, 나머지가2회 송신한다.','',
 '| 노드 | 위치 | Serial | Offered | RX | 손실 | PER | TX attempt/success | 비컨 | TX late |',
 '|---|---|---|---:|---:|---:|---:|---:|---:|---:|']
for serial,n in sorted(a['nodes_by_serial'].items(),key=lambda kv:kv[1]['physical_role']):
 role=n['physical_role'];lines.append(f"| {role} | {loc[role]} | {serial} | {n['offered']} | {n['rx']} | {n['offered']-n['rx']} | {n['per_percent']:.4f}% | {n['tx_attempts']}/{n['tx_success']} | {n['beacons']} | {n['delayed_tx_late']} |")
g=a['aggregate'];lines+=['',f"전체: {g['rx']}/{g['offered']}, PER {g['per_percent']:.4f}%. 노드별 <1%를 기준으로 판정한다.",'','| PHY 카운터 | 횟수 |','|---|---:|']
lines += [f'| {k} | {v} |' for k,v in errors.items()]
lines += ['',f'수신 완료 패킷의 source/관측 슬롯 소유자 불일치: {wrongsrc}. 별도 wrong-source 원문 카운터는 없으며 슬롯 분류에서 계산했다.','',
 '7대 READY/END 각각1개, 수집 timeout0, metadata/raw/payload hash 및 flash readback 검증 PASS. 원본 Git branch/HEAD/dirty/diff hash는 전후 동일하다. 실행 후 이번 lead26 이미지를 유지하고 지정된 7개 보드를 halt했다.','',
 '어제 NLOS lead26/K13의 정확한 RX/TX HEX를 논리 역할별로 재사용했다. 과거0% 실행은 역할 회전 block2, 이번은 차량의 고정 원래 역할이다. 환경과 역할 배치가 달라 동일 채널 비교나 차량 최적 lead 확정으로 해석하지 않는다. 한 번의 관측으로 장기 성능이나 PAC 우열을 확정하지 않는다.','',
 '## 상세 오류 원문','', '```text',*report['counters_raw_lines'],'```','',
 '## HEX SHA256','', '| 역할 | Serial | SHA256 |','|---|---|---|']
lines += [f"| {j['physical_role']} | {j['serial']} | {j['hex_sha256']} |" for j in c['jobs']]
lines += ['', '원문: [RX](capture1/results/local/init.log), [TX 및 제어](capture1/results/remote/status.json), [기계 판정](capture1/results/ASSESSMENT.json), [상세 JSON](RESULTS.json), [설치·조건](manifest.json).','']
(R/'RESULTS.md').write_text('\n'.join(lines))
print(json.dumps({'verdict':a['verdict'],'nodes':{n['physical_role']:n['per_percent'] for n in a['nodes_by_serial'].values()},'aggregate':g,'phy_errors':errors,'postflight':post},indent=2))
