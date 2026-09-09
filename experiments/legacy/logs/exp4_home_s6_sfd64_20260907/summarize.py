import pathlib,json,re,hashlib,datetime
root=pathlib.Path(__file__).resolve().parent;oldroot=root.parent/'exp4_home_s6_multislot_pac_ab_20260907';run=root/'R13P4S64_r1'
a=json.loads((run/'audit.json').read_text());b=json.loads((oldroot/'R13P4_r1/audit.json').read_text());m=json.loads((root/'manifest.json').read_text());order=json.loads((root/'actual_order.json').read_text())
assert len(order)==1 and a['valid'] and a['full_tx'] and a['total']['offered']==13000
assert a['total']['rx']==12909 and a['total']['lost']==91
post={}
def topo(s):return {'ids':re.findall(r'(?:J-Link|USB2\.1 Hub)@\w+.*?id (0x[0-9a-f]+)',s),'ports':re.findall(r'(?:J-Link|USB2\.1 Hub)@\w+',s),'loc':re.findall(r'"locationID" = (\d+)',s),'serial':re.findall(r'"USB Serial Number" = "(\d+)"',s)}
for side in ['local','remote']:
 before=json.loads((root/(side+'_preflight.json')).read_text());after=json.loads((root/(side+'_postflight.json')).read_text());post[side]={'git_unchanged':before['git']==after['git'],'probes_unchanged':set(before['probes'])==set(after['probes']),'usb_ids_ports_locations_serials_unchanged':topo(before['usb'])==topo(after['usb']),'remaining_capture_processes':after['capture_processes']};assert all(post[side][k] for k in ['git_unchanged','probes_unchanged','usb_ids_ports_locations_serials_unchanged']);assert not after['capture_processes']
timing=[l for l in (run/'init/init.log').read_text().splitlines() if l.startswith('BRRS_SLOT_TIMING_CSV,')]
for role,t in a['tx'].items():
 assert t['beacons']==1000 and t['beacon_missed']==0 and t['attempts']==t['success']==a['nodes'][role]['offered'] and t['delayed_late']==0 and t['end_received']==1
 text=(run/'tx'/(role+'.log')).read_text()
 applied=next(l for l in text.splitlines() if l.startswith('BRRS_DATA_PHY_APPLIED_CSV,'))
 assert 'sfd_to=33' in applied,applied
rows=['| '+r+' | '+str(m['roles'][r])+' | '+str(n['offered'])+' | '+str(n['rx'])+' | '+str(n['lost'])+' | '+f"{n['per_pct']:.3f}%"+' |' for r,n in a['nodes'].items()]
errnames=[('sfd_timeout','SFD timeout'),('phr_error','PHR error'),('crc_error','CRC error'),('rxfsl','RXFSL'),('fwto','FWTO'),('pto','PTO')]
errrows=[f"| {label} | {b['errors'][k]} | {a['errors'][k]} |" for k,label in errnames]
slots=a['init_records']['EXP4_SLOT_RX_CSV'];slotrows=[f"| {s['slot']} | N{s['owner']} | 1000 | {s['rx_good']} | {(1000-int(s['rx_good']))/10:.2f}% | {s['error']} | {s['timeout']} | {s['late']} |" for s in slots]
images=[f"| {r} | {rec['sha256']} |" for r,rec in m['variants']['R13P4S64'].items()]
body=f'''# M32/PAC4 · SFD timeout 64 · 1회 진단 결과

**N4 PER은 기존 동일 슬롯 배정 PAC4의 7.15%에서 이번 4.25%로 낮게 관측됐지만, 각 노드 PER <1% 목표는 실패했다.** 새 RF 실행은 정확히 1회이며, 기존 조건 반복은 하지 않았다. 전체는 12,909/13,000 수신·PER 0.700%다. 전체 평균으로 목표 통과를 판정하지 않는다.

## 변경과 검증

M32/PAC4, 물리 TX 6대·13 DATA 슬롯, sequence `2345672345673`, G250, lead 25us, SB/SP 3000/2500us, SF 10ms, 1,000 superframes를 사용했다. RX 창 122us/FWTO 119UUS, 슬롯별 delayed-RX, 무재전송 및 기존 비수신 구간 정책은 유지했다. **64는 SFD 대기 한도이며 프리앰블은 계속 32심볼이다.**

INIT DATA의 SFD timeout만 37→64심볼로 바꿨다. 첫 DATA 설정 후 레지스터를 한 번 읽는 진단과 종료 후 출력도 추가했다. 실제 readback은 64로 일치했다. 슬롯마다 추가 진단 SPI나 로그를 수행하지 않는다. ELF DATA config 비교는 SFD timeout 바이트 하나만 달랐고 SYNC config는 같았다. TX 6개 HEX는 직전 시험과 동일하며 실제 TX DATA 설정의 SFD timeout 33도 확인했다. 독립 소스 사본에서만 INIT를 수정했고 원본 Git 상태는 보존했다.

SSH s-macbook-air와 INIT 1050270933 및 지정 TX 6대가 정상 인식됐다. 현재 집 배치와 외부 전원 허브, 보드 방향·케이블·포트를 유지했다. RX는 사용자 설명의 세탁기 뒤 높이를 올린 위치이며 기존 TX 3대는 건조기 안이다. 추가 TX 3대의 정확한 물리 위치는 독립 계측하지 않았다. 집 배치는 차량 대여 전 준비 환경이며 NLOS 6.9m나 차량과 같은 채널로 간주하지 않는다.

실행 시작: {order[0]['started_at']}, 종료: {order[0]['finished_at']}. 이 시각은 준비·수집·검증을 포함하며 실제 RF는 약 10초다.

## 노드별 결과

| 역할 | Serial | 예정/송신 성공 | RX | 손실 | PER |
|---|---|---:|---:|---:|---:|
{chr(10).join(rows)}

모든 TX가 비컨 1,000개를 받았고 누락은 0이었다. 각 TX의 attempt와 success는 자기 예정량과 같았으며 합계 13,000회다. delayed-TX late 및 SYNC 수신 예약 late는 0, END 수신은 각 1회였다. 모든 7개 캡처의 READY/END 마커는 각각 1개이고 수집 timeout은 없었다. 7개 보드 모두 HEX 지정 flash 영역 readback을 통과했다.

## 기존 동일 슬롯 배정 PAC4와 오류 비교

기준은 이전 캠페인의 R13P4_r1(37심볼 한도)이다. 기준을 새로 재실행하지 않아 시간 변동을 통제한 A/B 결과는 아니다.

| 오류 | 기존 37 | 이번 64 |
|---|---:|---:|
{chr(10).join(errrows)}

wrong source/slot/superframe/length/config, delayed-RX late, SYNC delayed late/TX wait timeout, RDB mismatch/incomplete/recovered/resync/overrun, SPI 오류·timeout·recovery는 모두 0이었다. 13개 수신창 각각 attempted=armed=1,000, late=0이었고 good+error+timeout 합계가 예정량과 일치했다. 첫 RX 예약 여유 최소 1,313us, SYNC 준비 후 남은 예약 여유 최소 674us였다.

## 슬롯별 결과

슬롯 인덱스는 0부터 센다. N4의 두 슬롯에서 각각 3.9%와 4.6% 손실이 발생했다.

| 슬롯 | 소유자 | 예정 | RX | PER | 오류 | FWTO | late |
|---|---|---:|---:|---:|---:|---:|---:|
{chr(10).join(slotrows)}

## 해석과 다음 확인 대상

N4의 수신 성공 패킷 RMARKER 오차는 이번에도 248~264ns였다. 기존 7회에서도 같은 범위였다. 큰 스케줄 드리프트나 예약 실패가 성공 패킷에서 보이지 않지만, 이 값은 실패한 패킷의 도착 시각을 알려주지는 않는다.

SFD 대기 한도를 늘린 실행에서 N4 손실과 전체 SFD timeout이 더 적게 관측됐다. 이는 SFD 검출과 수신 종료 시점의 영향을 더 확인할 이유가 되지만, 한 번의 비동시 비교로 개선 원인을 확정할 수는 없다. 특히 PHR error도 33회 남았고 N5 손실은 기존 0에서 5개로 늘었다. 조기 프리앰블 오검출 하나로 원인을 확정하거나 SFD timeout을 계속 늘리면 해결된다고 추정하지 않는다.

이미 슬롯별 delayed-RX에서도 손실이 남으므로 continuous/manual-rearm만으로 모든 현상을 설명할 수 없다. 다음 확인 대상은 N4의 RF 획득 품질(CIR/수신 전력/누산량)과 오류 발생 시점이다. 해당 진단은 수신 처리에 영향을 주지 않도록 설계해야 하며 이번에는 추가하지 않았다. 이 설정을 최종 채택하거나 여러 환경에서 PER <1%를 보장하지 않는다. 긴 반복과 최종 차량 검증도 아직 하지 않았다.

Qorvo DW3xxx API Guide p34는 SFD timeout을 잘못된 프리앰블 검출에서 복구하기 위한 장치로 설명하고 권장식을 preamble+1+SFD−PAC로 제시한다. 현재 권장식은 37이며 64는 이번 진단값이다. [제조사 API 문서](https://forum.qorvo.com/uploads/short-url/xD3TlXKvkujjdUaJWXv2b4E7GQN.pdf)

## 최종 장비 및 보존 상태

현재 INIT는 **M32/PAC4·SFD timeout 64·13슬롯·추가 슬롯 N3** 시험 이미지이며 모든 보드는 실행 종료 상태다. 별도 복원 플래시나 추가 RF 실행은 하지 않았다. 로컬·원격의 Git branch/HEAD/dirty/diff SHA와 USB registry/port/location/serial이 실행 전후 동일했다. 잔여 캡처 프로세스는 없다. commit/push 및 GitHub 업로드는 하지 않았다.

| 이미지 | SHA256 |
|---|---|
{chr(10).join(images)}

원시 RTT 및 상태는 R13P4S64_r1/init, tx에, 검증은 audit.json과 flash_readback.json에 보존했다. manifest.json, compiled_config_evidence.json, source_change.patch, runtime_source_hashes.json, prior_timing_review.json에 설정·변경·근거가 있다. 원본 회귀 변경이 아닌 독립 시험 소스는 `{m['source_trial']}`다.
'''
(root/'RESULTS.md').write_text(body)
(root/'RESULTS.json').write_text(json.dumps({'generated_at':datetime.datetime.now().astimezone().isoformat(),'new_rf_runs':1,'baseline':str(oldroot/'R13P4_r1'),'baseline_N4_per':b['nodes']['N4']['per_pct'],'trial':a,'manifest':m,'actual_order':order,'postflight':post,'successful_packet_timing':timing,'interpretation':'Lower observed N4 PER but target failure; single noncontemporaneous comparison, no causal or cross-environment guarantee.','final_board_variant':'R13P4S64','additional_RF_runs':0},ensure_ascii=False,indent=2)+'\n')
(root/'postflight_verification.json').write_text(json.dumps(post,indent=2)+'\n')
print(json.dumps({'new_RF_runs':1,'valid':a['valid'],'goal':a['goal'],'total':a['total'],'postflight':post,'report':str(root/'RESULTS.md')},ensure_ascii=False,indent=2))
