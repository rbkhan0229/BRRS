#!/usr/bin/env python3
"""Read saved evidence and update the preparation coverage report; no RF."""
import json
from pathlib import Path
import sys
from datetime import datetime
from collections import Counter

R=Path(__file__).resolve().parent
API=Path('/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API')
sys.path.insert(0,str(API))
from brrs_suite_case import checked,save,sha

plan=json.loads((R/('ACTIVE_PLAN.json' if (R/'ACTIVE_PLAN.json').exists() else 'PLAN.json')).read_text())
total=len(plan['cases'])
records=json.loads((R/'observations.json').read_text()) if (R/'observations.json').exists() else {}
remaining=json.loads(Path(plan['remaining_evidence']).read_text())
analysis=json.loads((R/'analysis_index.json').read_text()) if (R/'analysis_index.json').exists() else {}
inventory={}
counts=Counter()
per_fail=[]
lines=['# 차량 전 각 조건1회 기능 점검 · 2026-09-08', '',
       f'진행: 새 조건 **{len(records)}/{total}**. 실제 환경은 사용자 설치 NLOS6.9m이며 차량 측정이 아니다. 기존 완료 조건은 기능 실행 증거로만 재사용하며, 다른 lead·회전·시각·전선 가림 전후 PER를 합산하지 않는다.', '',
       'PAC8 lead26µs, PAC4 lead27µs를 이번 준비의 고정값으로 사용했다. PAC4의27µs는 안정적인 PER<1% 선정값이 아니다. 각각1회 실행하고 유효한 PER 실패는 반복하지 않았다. 원본 Git과 펌웨어 C를 바꾸지 않으며 commit/push는 하지 않는다.', '',
       'RX1050270933, 단일 링크 및 Exp4 S1 TX는 물리N4/1050282818. Exp4 S2~S6은 block1 설치표 매핑이다. TX허브 외부 전원은 직전 사용자 확인을 기록했고 이번 측정 중 물리 배치·배선·전원을 변경하지 않았다. SSH·USB 구성·원본 Git 상태는 preflight_local/remote.json에 있다.', '',
       '| 단계 | 이번 대상 | 완료 | 노드별 PER<1% | PER 실패 |',
       '|---|---:|---:|---:|---:|']
for a in records.values():
    c=checked(Path(a['bundle'])); p=c['conditions']; stage=p['stage']
    counts[(stage,'complete')]+=1;counts[(stage,a['verdict'])]+=1
    if a['verdict']!='PASS':per_fail.append(a['case_id'])
    inventory[a['case_id']]={'bundle':a['bundle'],'conditions':p,'manifest_sha256':c['manifest_file_sha256'],
        'payload_index_sha256':sha(Path(a['bundle'])/'payload_hashes.json'),'prepared_at':c['prepared_at'],
        'jobs':[{k:j[k] for k in ['physical_role','logical_node','serial','hex','hex_sha256','elf_sha256']} for j in c['jobs']]}
for stage in ['exp1','exp2','exp4']:
    n=sum(c['conditions']['stage']==stage for c in plan['cases'])
    lines.append(f'| {stage} | {n} | {counts[(stage,"complete")]} | {counts[(stage,"PASS")]} | {counts[(stage,"FAIL_PER")]} |')
lines+=['',f'Exp1/2 CSV·그림 후처리 완료: {len(analysis)}/13조건. Stage0·Exp3 A/B/C·Exp5는 앞선 대표 실행과 후처리 증거를 재사용한다.', '',
        '## 이번 실행별 물리 노드 PER (%)', '',
        '| # | 조건 | N2 | N3 | N4 | N5 | N6 | N7 | 판정 |',
        '|---:|---|---:|---:|---:|---:|---:|---:|---|']
for i,c in enumerate(plan['cases'],1):
    cid=c['id']
    if cid not in records:continue
    a=records[cid];p=c['conditions'];nodes={n['physical_role']:n for n in a['nodes_by_serial'].values()}
    label=f'{p["stage"]} M{p["preamble"]}/P{p["rx_pac"]}/L{p["lead_us"]}'
    if p['stage']=='exp4':label+=f'/S{p["sensors"]}/K{len(p["slot_owners"])}'
    values=[f'{nodes[r]["per_percent"]:.3f}' if r in nodes else '—' for r in ['N2','N3','N4','N5','N6','N7']]
    lines.append(f'| {i} | [{label}]({cid}/results/ASSESSMENT.json) | '+' | '.join(values)+f' | {a["verdict"]} |')
lines+=['', '## 판정·원문과 차량 인계', '',
        '추가 CSV·그림·심층 분석은 사용자 요청에 따라 나중으로 미뤘다. Exp1/2의13조건 후처리는 요청 전에 이미 완료했다. 수집·역할·HEX readback·READY/END 성공과 노드별 PER<1%를 구분한다. 미참여 노드는 표에서 —로 표시한다. offered/RX/TX/beacon/late, 오류 카운터, 원문 hash는 observations.json 및 각 bundle/results에 보존한다. 해당 단계가 출력하지 않는 카운터를0으로 가정하지 않는다. Exp2 CIR는 성공 패킷 표본이며 PER와 유효 CIR행 수를 함께 본다.', '',
        '사용자 요청에 따라 남은 S1~S5 조건을 생략하고 TX6의 미확인16조건만 완료 대상으로 줄였다. 전체58개 Exp4 조합을 이번에 모두 실행한 것은 아니다. 조건1회 점검은 논문 반복·전 역할 회전·차량 성능 검증 또는 안정적인 최대 용량 확정이 아니다. 오늘의 PER 실패 때문에 추가 lead 최적화나 동일 RF를 반복하지 않는다.', '',
        '[차량 아침 실행 안내](VEHICLE_MORNING.md) · [차량 환경 기록 틀](vehicle_manifest_TEMPLATE.json) · [최종 선택 계획](ACTIVE_PLAN.json) · [HEX/일련번호 목록](image_inventory.json) · [재사용 증거와 남았던 목록](../vehicle_onepass_remaining_20260908/REMAINING.json)', '']
if (R/'postflight_summary.json').exists():
    post=json.loads((R/'postflight_summary.json').read_text())
    lines+=['종료 확인: '+post['description'],'']
save(R/'image_inventory.json',inventory)
save(R/'RESULTS.json',{'updated_at':datetime.now().astimezone().isoformat(),'new_conditions_complete':len(records),'new_conditions_planned':total,
    'per_fail_cases':per_fail,'per_pass_count':sum(a['verdict']=='PASS' for a in records.values()),
    'analysis_complete':len(analysis),'selected_capture_coverage_complete':len(records)==total,'full_original_68_coverage_complete':len(records)==68,'scope_change':plan.get('scope_change'), 'additional_postprocessing_deferred_by_user':True,
    'paper_repetitions_complete':False,'vehicle_measured':False,'prior_coverage':remaining,'observations':records})
(R/'RESULTS.md').write_text('\n'.join(lines))
print('REPORT_UPDATED',len(records),'cases',len(per_fail),'PER_fail',len(analysis),'analyses')
