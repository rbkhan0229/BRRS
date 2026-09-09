import datetime,hashlib,json,pathlib,re
root=pathlib.Path(__file__).resolve().parent
m=json.loads((root/'manifest.json').read_text());order=json.loads((root/'actual_order.json').read_text());runs=[]
for item in order:
 p=root/item['tag']/'audit.json'
 if p.exists():
  a=json.loads(p.read_text());a['tag']=item['tag'];a['orchestration']=json.loads((root/(item['tag']+'_orchestration.json')).read_text());runs.append(a)
post={}
def topo(s):return {'ids':re.findall(r'(?:J-Link|USB2\.1 Hub)@\w+.*?id (0x[0-9a-f]+)',s),'ports':re.findall(r'(?:J-Link|USB2\.1 Hub)@\w+',s),'loc':re.findall(r'"locationID" = (\d+)',s),'serial':re.findall(r'"USB Serial Number" = "(\d+)"',s)}
for side in ['local','remote']:
 before=json.loads((root/(side+'_preflight.json')).read_text());after=json.loads((root/(side+'_postflight.json')).read_text());post[side]={'git_unchanged':before['git']==after['git'],'probes_unchanged':set(before['probes'])==set(after['probes']),'usb_ids_ports_locations_serials_unchanged':topo(before['usb'])==topo(after['usb']),'remaining_capture_processes':after['capture_processes']};assert all(post[side][k] for k in ['git_unchanged','probes_unchanged','usb_ids_ports_locations_serials_unchanged']);assert not after['capture_processes']
blocks={}
for prefix,extra in [('F','N2'),('R','N3')]:
 selected=[a for a in runs if a.get('variant','').startswith(prefix)]
 if len(selected)==3:
  rows={a['tag']:a for a in selected};before=rows[prefix+'13P8_r1'];mid=rows[prefix+'13P4_r1'];after=rows[prefix+'13P8_r2'];nodes={}
  for role in m['roles']:
   if role=='init':continue
   b=before['nodes'][role];c=mid['nodes'][role];d=after['nodes'][role];pooled=100*(b['lost']+d['lost'])/(b['offered']+d['offered']);nodes[role]={'pac8_before_pct':b['per_pct'],'pac4_pct':c['per_pct'],'pac8_after_pct':d['per_pct'],'pac8_pooled_pct':pooled,'pac4_minus_pac8_pooled_pp':c['per_pct']-pooled}
  blocks[prefix]={'extra_slot_owner':extra,'nodes':nodes,'all_system_valid':all(a['valid'] for a in selected),'all_full_tx':all(a.get('full_tx',False) for a in selected)}
summary={'generated_at':datetime.datetime.now().astimezone().isoformat(),'manifest':m,'actual_order':order,'runs':runs,'blocks':blocks,'postflight':post,'goal_passing_runs':[a['tag'] for a in runs if a.get('goal_pass')],'total_runs':len(runs),'final_variant':runs[-1].get('variant') if runs else None,'limitations':['Each PAC/slot-allocation block has two bracketing PAC8 runs and one PAC4 run, around10seconds RF per run.','Different schedules kept separate; 12-slot baseline not pooled into13-slot PAC ranking.','Both physical six-node setup and newly built S6 TX firmware differ from earlier S3 runs; do not attribute differences solely to node count.','Raw PHY counters do not establish a unique RF cause; no CIR/RSSI/CFO or energy measurement in this campaign.','Fixed synthetic test payload per scheduled slot, no ACK-dependent or error-dependent retransmission.']}
(root/'RESULTS.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n');(root/'postflight_verification.json').write_text(json.dumps(post,indent=2)+'\n')
roles=[r for r in m['roles'] if r!='init'];rows=[];errorrows=[];slotrows=[]
for a in runs:
 if 'nodes' not in a:continue
 v=m['parameters_by_variant'][a['variant']];cols=[f"{a['nodes'][r]['per_pct']:.3f}%" for r in roles];rows.append('| '+a['tag']+' | '+str(v['slots'])+' | '+str(v['PAC'])+' | '+' | '.join(cols)+f" | {a['total']['rx']}/{a['total']['offered']} | {a['goal']} |")
 e=a['errors'];errorrows.append('| '+a['tag']+' | '+' | '.join(str(e[k]) for k in ['sfd_timeout','phr_error','crc_error','rxfsl','fwto','pto'])+f" | {sum(t['beacon_missed'] for t in a['tx'].values())} |")
 for w in a['init_records']['EXP4_SLOT_RX_CSV']:
  slotrows.append({'run':a['tag'],'slot':int(w['slot']),'node':'N'+w['owner'],'rx':int(w['rx_good']),'offered':1000,'per_pct':(1000-int(w['rx_good']))/10,'errors':int(w['error']),'timeouts':int(w['timeout']),'late':int(w['late'])})
(root/'slot_results.json').write_text(json.dumps(slotrows,indent=2)+'\n')
comparison=[]
for b,block in blocks.items():
 comparison.append(f"### 추가 슬롯 {block['extra_slot_owner']}\n\n| 노드 | PAC8 전 | PAC4 | PAC8 후 | PAC8 전후 합산 |\n|---|---:|---:|---:|---:|")
 for r,n in block['nodes'].items():comparison.append(f"| {r} | {n['pac8_before_pct']:.3f}% | {n['pac4_pct']:.3f}% | {n['pac8_after_pct']:.3f}% | {n['pac8_pooled_pct']:.3f}% |")
 comparison.append('')
images=[]
for name,recs in m['variants'].items():images.append(f"| {name} INIT | {recs['init']['sha256']} |")
for r in roles:images.append(f"| 공통 {r} ({m['roles'][r]}) | {m['variants']['L12P8'][r]['sha256']} |")
valid=all(a.get('valid') for a in runs);full=all(a.get('full_tx') for a in runs)
body=f'''# 집 모사환경 · 물리 TX6 · 12/13슬롯 PAC8/PAC4 비교

실행 수 {len(runs)}회. 각 실행은 1,000 superframes(약 10초 RF)이며, 모든 노드 PER <1%가 목표다. 목표 통과 실행: {', '.join(summary['goal_passing_runs']) or '없음'}.

**현재 집 배치에서는 PAC4가 PAC8보다 N4 PER을 낮췄지만, 최저 7.15%로 목표에는 실패했다.** N4(1050282818)를 제외한 5개 노드는 모든 실행에서 PER 0~0.1%였다. 예정된 90,000회 송신은 모두 성공했고 88,503개를 수신했다. 비컨 누락, 송수신 예약 지연, 슬롯/소스 오류, 버퍼 및 수집 오류는 없었다.

동일한 13슬롯 배정에서 PAC8 → PAC4 → PAC8 순서로 비교했다. N2에 추가 슬롯을 준 조건의 N4 PER은 10.55% → 7.55% → 13.30%, N3에 추가 슬롯을 준 조건은 16.30% → 7.15% → 9.90%였다. 두 조건 모두 PAC4가 앞뒤 PAC8보다 낮았다. 다만 PAC8 실행 사이에도 변동이 크고 PAC4는 배정별 1회이므로, 모든 환경에서의 우열이나 개선 폭을 확정할 수 없다.

이번 12/13슬롯 부하에서 예약·처리 오류는 관측되지 않았다. 남은 손실은 주로 N4의 두 수신 슬롯에서 발생했다. 이미 슬롯별 scheduled delayed-RX를 사용한 결과이므로, continuous/manual-rearm만으로 모든 고손실을 설명할 수는 없다. PAC4에서 SFD timeout은 감소했지만 PHR error가 늘었다. 수신 단계의 양상이 달라졌다는 증거이며, RF 원인을 특정한 결과는 아니다. 다음 개선은 같은 M32·물리 배치에서 N4의 수신 타이밍과 RF 진단을 함께 확인하는 방향이 타당하다. PAC 선택은 다른 모사 배치와 최종 차량에서도 반복 확인해야 한다.

실제 순서 및 정확한 KST 시각은 actual_order.json과 각 orchestration 파일에 보존했다.

## 조건과 역할

최종 차량 시험에 대비한 집 모사 환경이다. RX는 최근 사용자 설명의 세탁기 뒤 높이를 올린 배치이며 기존 TX 3대는 건조기 안이다. 추가 3대의 세부 위치는 독립 계측하지 않고 연결된 현재 상태를 유지했다. 허브 외부 전원은 사용자가 확인했으며, 새 하위 USB 분기의 급전 상세는 확인되지 않았다. 배치·방향·케이블·포트·전원을 바꾸지 않았다.

SSH s-macbook-air 접속은 정상이었다. INIT=1050270933, N2=1050211584, N3=1050273888, N4=1050282818, N5=1050208509, N6=1050227627, N7=1050204212. 실장비 serial을 확인한 7대만 플래시했다. 각 측정 시작 전 INIT 및 모든 TX를 지정 serial로 halt한 뒤 TX를 순서대로 준비하고 전부 READY를 확인한 후 INIT를 시작했다. 종료 뒤 HEX가 지정한 flash 영역을 읽어 이미지와 일치함을 확인했다.

기존 P25 런타임 소스를 독립 비Git 사본에서 그대로 사용하고 S6/sequence/PAC 빌드 옵션만 변경했다. 원본 Src 파일 SHA가 모두 일치함을 확인했다. 빌드 도구의 경로 설정과 수집·검증 도구를 새 사본 및 6노드에 맞췄다. TX도 S6로 새로 빌드했으므로 과거 S3 TX HEX를 사용한 시험과 구분한다. 모든 PAC 시험에서 TX 6개 HEX는 동일하다.

M32/G250/lead 25us/SB 3000us/SP 2500us/SF 10ms, 슬롯 347us, 슬롯별 bounded delayed-RX 및 SPI 최적화, RX 창 122us/FWTO 119UUS. DATA PAC8의 SFD timeout은 33, PAC4는 37이다. ELF에서 DATA PAC enum과 파생 SFD timeout만 다르고 SYNC 설정은 동일함을 확인했다. TX와 비컨은 PAC8 고정이다. ACK나 오류에 따른 재전송은 없다. 같은 노드의 여러 슬롯은 별개의 예정 송신 기회로 모두 분모에 포함한다. 데이터는 기존 고정 시험 payload이며 실제 센서의 신규 샘플은 아니다.

## 슬롯 배정과 PER

물리 TX 6대와 DATA 슬롯 수는 별개다. 현재 타이밍 설정의 슬롯 한도는 13이고 비컨 포맷의 표현 한도는 32이며, 이 값들이 BRRS 자체의 최대 슬롯 수를 뜻하지는 않는다.

L12P8은 `234567234567`: 각 노드 2슬롯·2,000보고, 총 12,000보고. F13은 `2345672345672`: N2만 3슬롯·3,000보고, 나머지는 2,000보고, 총 13,000보고. R13은 `2345672345673`: N3만 3,000보고, 나머지는 2,000보고다. 같은 배정 안에서 PAC8 → PAC4 → PAC8을 비교했다. 더 많은 슬롯을 맡는 노드가 있으므로 노드별 PER 분모를 맞췄다.

| 실행 | 슬롯 | PAC | N2 | N3 | N4 | N5 | N6 | N7 | 전체 RX/예정 | 목표 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
{chr(10).join(rows)}

비컨 누락·미송신도 예정량 기준 PER에 포함한다. 실제 TX 기준 손실률과 노드별 TX/beacon/attempt/success는 RESULTS.json에 별도 기록했다. 모든 TX의 예정 송신 완료={full}, 모든 실행의 시스템/수집 무결성 통과={valid}.

## 오류

| 실행 | SFD timeout | PHR | CRC | RXFSL | FWTO | PTO | TX 비컨누락 합계 |
|---|---:|---:|---:|---:|---:|---:|---:|
{chr(10).join(errorrows)}

각 실행에서 TX 6대 모두 비컨 1,000개 수신, 누락·중복·sequence gap 0이었다. TX attempt와 success는 각각 자기 노드의 예정 보고량과 일치했고 delayed-TX late는 0이었다. wrong source/slot/superframe/length/config, delayed-RX late, SYNC delayed late 및 TX wait timeout은 모두 0이었다. RDB mismatch/incomplete/recovered/resync/overrun 및 SPI error/timeout/recovery도 모두 0이었다. 모든 보드 캡처에서 READY와 END 마커가 각각 1개였고 RTT 수집 timeout은 0이었다.

슬롯별 오류·timeout·late와 PER는 slot_results.json에 보존했다. 각 audit는 위 카운터와 원시 로그 hash, 실제 serial, flash readback, 수신창 armed/terminal 합계 및 송신량을 검증한다. FWTO는 무선 수신창 timeout이며 RTT 수집 timeout과 다르다. 상세 카운터는 각 audit의 init_records/tx와 final_counter_checks.json에 있다.

## 같은 슬롯배정 안의 PAC 비교

{chr(10).join(comparison)}

PAC8 전후 합산은 동일 슬롯 배정에 한정한 참고치다. PAC4는 각 배정 1회, 전후 PAC8은 각 1회이므로 시간 변동을 완전히 제거한 인과 검증은 아니다. 12슬롯과 13슬롯을 합산해 PAC 순위를 내지 않으며, 추가 슬롯 소유자가 다른 조건도 구분한다. 최종 선택은 전체 평균보다 가장 나쁜 노드와 반복 안정성을 기준으로 한다. 차량 및 다른 물리 배치에서 재검증이 필요하다.

## 보존 및 한계

초기·종료 USB registry/port/location/serial 및 Git branch/HEAD/dirty/diff SHA가 동일했다. 잔여 실험 프로세스는 없다. 원본 저장소 변경·commit·push는 없으며 GitHub 업로드 보류를 유지한다. 마지막 실행 설정은 {summary['final_variant']}, 즉 TX 6대·13슬롯·PAC8·추가 슬롯 N3다. 종료 후 7대 모두 flash readback을 통과했다.

이번 전체 RF 관측은 약 {len(runs)*10}초이며 장기 PER 보증이 아니다. 신규 6대 배치 및 S6 TX 빌드가 과거 S3 조건과 달라 과거 N3에서 현재 N4로 약한 노드가 바뀐 이유를 노드 수 증가 하나로 단정하지 않는다. 집 배치를 NLOS 6.9m나 차량과 같은 채널로 간주하지 않는다. 수신 오류 카운터만으로 잡음·금속·다중경로·CFO·continuous RX 중 단일 원인을 확정하지 않는다. RF 비수신 구간 정책은 기존 코드를 유지했으며 전류/에너지는 직접 측정하지 않았다.

## HEX SHA256

| 이미지 | SHA256 |
|---|---|
{chr(10).join(images)}

manifest.json, compiled_config_evidence.json, runtime_source_hashes.json, source_copy.json에 구성과 출처를 보존했다. 각 run의 init 및 tx 디렉터리에 원시 RTT/console/status, 각 run에 manifest/readback/audit가 있다. audit_regression_checks.json은 과거 성공·PHY 실패·비컨 누락·RX 0 및 의도적으로 틀린 분모를 구별하는 검증 결과다.
'''
(root/'RESULTS.md').write_text(body)
print(json.dumps({'runs':len(runs),'passing_runs':summary['goal_passing_runs'],'postflight':post,'report':str(root/'RESULTS.md')},ensure_ascii=False,indent=2))
