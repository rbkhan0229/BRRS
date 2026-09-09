# NLOS 6.9m · M32 burst / 슬롯별 scheduled-RX A/B

**N3 합산 PER: A 22.55% → B 2.90% (상대 감소 87.1%).** B의 전체 합산 PER은0.967%지만 N3가2.90%여서 각 노드 PER<1% 목표는 미달이다.

## 조건과 연결

사용자 확인 NLOS6.9m, 외부 전원 어댑터가 연결된 TX USB 허브. ssh s-macbook-air 정상. INIT1050270933는 로컬, TX N2=1050211584 / N3=1050273888 / N4=1050282818는 원격에서 실제 J-Link 열거로 확인했다. 다른 serial에 플래시하지 않았다. 물리 배치·방향·케이블·허브 포트·전원·TX 출력은 바꾸지 않았다.

M32/PAC8/S3/G250/lead15us/SB3000us/SP2500us/1000SF, SF10000us, 슬롯347us, PSDU26B/app16B, owners234, 재전송 없음. A/B 모두 동일한 기존 TX 세 HEX를 사용했다. 모든 최종 실행에서 각 TX는 beacon 수신/attempt/success=1000/1000/1000, delayed-TX late0, END1이다.

## 비교 구현

A는 HEAD55a23fa의 기존 첫-slot delayed-RX + 후속 CMD_RX manual burst다. B는 같은 소스에서 BRRS_EXP4_SLOTTED_RX=1로 빌드했다. 원본 분석 repo의 Git 상태는 보존하고 별도 소스 사본에서 구현했다. 최종 소스를 flag0으로 재빌드한 A HEX가 최초 A와 byte 단위로 같은 SHA256임을 확인했다. 그러므로 이번 비교를 과거 rev24와 현재 rev48 차이로 설명하지 않는다. A의 출발 소스는 현재 rev48이며, 8월25일 HEX는 TX에 그대로 사용했다.

B는 각 슬롯 시작을 비컨의 절대 시각에 예약한다. 첫 창에서 FWTO110UUS(약112.82us, 요청 창112us)를 설정하고 후속 창에도 유지한다. 정상 수신은 CIA 완료 및 FINFO/RX_TIME 저장 후 다음 DX_TIME/CMD_DRX를 예약하고 헤더 처리·buffer release를 이어간다. 오류는 상세 상태를 읽고 지운 후 다음 창을 예약한다. 각 창은 한 번의 수신 시도이며, 성공·오류·FWTO 뒤 다음 슬롯으로 간다. 즉시RX fallback 또는 TX 재전송은 없다.

A/B의 SPI 모드는 모두 legacy per transaction, polling, PHY fast switch/IRQ/추가 진단 OFF다. B의 후속 예약은 매회 HPDWARN을 확인한다. 불필요한 SYS_TIME SPI read를 후속 예약 경로에 추가하지 않아, 후속 슬롯의 min-arm-slack은 미측정으로 구분한다. legacy rearm service=0은 B에서 쓰지 않는 통계이며, B의 유효성은 슬롯별 예약·late·terminal counts로 판정한다. RX outside DATA burst 및 TX의 다음 비컨 scheduled-RX 정책은 유지했다. deep sleep 또는 전류 절감량을 계측한 실험은 아니다.

## 최종 교차 실행 결과

아래 네 실행은 동일한 최종 A/B HEX로 순서대로 실시했다. 각 행은 노드당1000 offered다.

| 순서·실행 | N2 RX / PER | N3 RX / PER | N4 RX / PER | 전체 RX / PER | 목표 |
|---|---:|---:|---:|---:|---|
| 1. A4 | 981 / 1.90% | 771 / 22.90% | 997 / 0.30% | 2749/3000 / 8.367% | FAIL_PER |
| 2. B5 | 1000 / 0.00% | 974 / 2.60% | 1000 / 0.00% | 2974/3000 / 0.867% | FAIL_PER |
| 3. A5 | 990 / 1.00% | 778 / 22.20% | 996 / 0.40% | 2764/3000 / 7.867% | FAIL_PER |
| 4. B6 | 1000 / 0.00% | 968 / 3.20% | 1000 / 0.00% | 2968/3000 / 1.067% | FAIL_PER |

| 방식·2회 합산 | N2 | N3 | N4 |
|---|---:|---:|---:|
| A | 1971/2000 / 1.45% | 1549/2000 / 22.55% | 1993/2000 / 0.35% |
| B | 2000/2000 / 0.00% | 1942/2000 / 2.90% | 2000/2000 / 0.00% |

실행 시각은 호스트 캡처·검증 구간(KST)이다. 각 RF 실험 자체는1000SF≈10초다.

- A4: 2026-09-07T10:40:38.973958+09:00 – 2026-09-07T10:41:11.178288+09:00
- B5: 2026-09-07T10:44:14.625779+09:00 – 2026-09-07T10:44:41.535165+09:00
- A5: 2026-09-07T10:44:42.783623+09:00 – 2026-09-07T10:45:14.885852+09:00
- B6: 2026-09-07T10:45:16.141222+09:00 – 2026-09-07T10:45:47.911068+09:00

## 오류 카운터와 검증

| 실행 | RX 오류 합계 | SFD timeout | PHR | CRC | RXFSL | FINT-only | FWTO/PTO |
|---|---:|---:|---:|---:|---:|---:|---:|
| A4 | 190 | 0* | 1* | 0* | 0* | 189 | 0/0 |
| B5 | 26 | 0 | 5 | 0 | 21 | 0 | 0/0 |
| A5 | 183 | 0* | 1* | 0* | 0* | 182 | 0/0 |
| B6 | 32 | 0 | 1 | 1 | 30 | 0 | 0/0 |

A는 기존 CMD_RX 이후 상세 상태를 읽는 경로여서 많은 오류 subtype이 FINT-only로 남는다. 별표(*)는 로그에 남은 관측값이며 완전한 집계가 아니다. Raw의 subtype0을 실제 해당 오류가 없었다는 뜻으로 해석하지 않았다. B는 예약 전에 오류 상세를 읽어 분류한다. 따라서 subtype 분포의 직접 비교에는 읽기 순서 차이가 있다. A는 FWTO가 꺼져 있고 B는 켜져 있어 FWTO의 단순 증감을 감도 차이로 해석하지 않는다.

네 실행 모두 wrong length/slot/superframe, config error, delayed RX/TX late, RDB mismatch/incomplete/recovered/resync/overrun 및 수집 timeout0. 보드별 READY/END 마커, 종료코드, RX 합계, slot/source 매칭, 네 HEX 해시와 populated flash bytes readback을 확인했다. 독립적인 wrong-source counter가 없는 형식은 source→observed owner 일치로 확인했다. 유효RX0인 실행은 PASS로 허용하지 않았다.

| B 실행·슬롯 | 시도/예약 | late | 정상RX | timeout | PHY error | terminal 합계 |
|---|---:|---:|---:|---:|---:|---:|
| B5 / N2 | 1000/1000 | 0 | 1000 | 0 | 0 | 1000 |
| B5 / N3 | 1000/1000 | 0 | 974 | 0 | 26 | 1000 |
| B5 / N4 | 1000/1000 | 0 | 1000 | 0 | 0 | 1000 |
| B6 / N2 | 1000/1000 | 0 | 1000 | 0 | 0 | 1000 |
| B6 / N3 | 1000/1000 | 0 | 968 | 0 | 32 | 1000 |
| B6 / N4 | 1000/1000 | 0 | 1000 | 0 | 0 | 1000 |

## 해석

교차 반복에서 A가 높고 B가 낮아지는지 위 표로 판단한다. 같은 M32·TX·배치에서 수신 창과 재진입·오류 진행 방식을 바꾼 효과이므로, 약한 N3 링크의 손실이 수신 제어 방식에 민감하다는 근거다. 다만 이 A/B는 탐색 시간뿐 아니라 window timeout, 오류 진행, 완료 메타데이터를 읽는 순서도 함께 바뀐다. 잡음이 누적됐다는 단일 기전이나 AGC/CIA 등 특정 내부 상태를 직접 입증하지 않는다.

DW3000 delayed-RX도 예정 시각에 켜진 뒤 preamble hunt를 한다. 따라서 BRRS의 이상적인 검출 생략을 구현한 것은 아니다. 후속 슬롯의 예정된 수신 창을 구현하여 BRRS 전제를 더 직접적으로 시험한 결과다. RXFSL은 데이터 Reed–Solomon 복호화 오류이며, 이 카운터만으로 원인을 잡음·금속·continuous-RX 중 하나로 확정하지 않는다.

각 노드 PER<1%를 충족하지 못하는 노드가 있으면 최종 목표는 미달이다. B에서 남는 오류는 동일 M32·동일 배치에서 수신 창 경계와 획득/동기화/채널 추정 상태를 추가 구분할 대상이다. 이번 턴에서는 A/B 구현·검증과 비교까지 수행했고 M64, PAC, 출력, 위치 변경으로 우회하지 않았다.

## 개발 단계 기록과 제외 사유

아래 실행도 원본을 보존했다. 최종4회와 합산하지 않는다.

| 실행 | N2 / N3 / N4 PER | 상태 |
|---|---|---|
| A1 | 1.1 / 21.9 / 0.3% | 정상 초기 기준선 |
| B1 | 원본 보존, PER 비교 제외 | wrong-length241, wrong-slot65; metadata 읽기와 다음 RX 시작이 겹칠 수 있는 경로 수정 |
| A2 | 1.0 / 22.9 / 0.5% | 정상 개발 기준선 |
| B2 | 0.1 / 2.5 / 0% | 모든 window/metadata/slot/RDB 검증 수치는 정상. 기존 zero-timeout·즉시rearm 판정과 비호환, raw FAIL 보존 |
| A3 | 1.0 / 15.9 / 0.7% | 정상 개발 기준선 |
| B3 | 0.1 / 2.4 / 0% | 모든 window/metadata/slot/RDB 검증 수치는 정상. 기존 즉시rearm 카운터 검사와 비호환, raw FAIL 보존 |
| B4 | 0.1 / 3.2 / 0.1% | 최종 펌웨어의 RX/수집 검증 PASS. N4 비컨1회 누락, TX999회로 완전 송신 비교 조건 미달 |

B2 이후 수신 경로를 다시 바꾸지 않고 종료 판정을 B의 실제 동작에 맞췄다. 모든 슬롯의 armed1000, late0, good+timeout+error1000, 전체 RX/오류/timeout 일치 및 상태 처리 횟수를 요구한다. 정상적인 창 만료를 손실로 집계하며 다른 시스템 오류를 허용하지 않는다. 수정 후 최종 A4/B5/A5/B6를 별도로 수집했다. B4는 N4 비컨1회 누락으로 TX999회여서 사전의 완전 송신 조건에 따라 별도 보존했다. 이 B4에서도 N3 PER은3.2%였으며, 제외된 결과를 숨기지 않는다. 사용자가 통행/교란을 알린 실행은 없었다.

## 펌웨어 및 재현 자료

| 역할·방식 | serial | SHA256 |
|---|---|---|
| INIT A | 1050270933 | `ec5dc769c8eec8c59c0c98173a858881199eb989777689fa7e4ed11f78f09e28` |
| INIT B | 1050270933 | `2234d3e8468572c9396e9b9abf108691400868f88eb66c43bde021be12df2f26` |
| N2 공통 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 공통 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 공통 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

- `manifest.json`과 각 run의 `manifest.json`: 역할·설정·이미지·수집기 해시. 개발 중 manifest는 버전별 보존.
- `RESULTS.json`, `RESULTS.csv`: 최종4회 및 방식별·노드별 합산.
- 각 run의 init/tx raw log, status.json, audit.json, init/tx_flash_readback.json.
- `implementation.patch`: 원본 HEAD55a23fa 대비 변경. 별도 소스 `DW3_QM33_SDK_1.0.2_exp4_slotrx_ab_20260907`에만 구현.
- `A_rebuild_equivalence_final_v5.json`: 최종 소스 flag0의 A byte 동일성.
- `local_preflight.json`, `remote_preflight.json`, postflight 파일: 연결·프로세스·Git 보존 검증.

종료 후 로컬·원격 probes, USB registry ID/포트/serial 및 기존 Git branch/HEAD/dirty/diff hash가 실행 전과 같은 것을 확인했다. 잔여 캡처 프로세스는 없다. commit/push는 하지 않았다. 최종 보드는 INIT=B, TX=기존 정확한 세 HEX 상태이며 실험은 END 이후 종료했다.
