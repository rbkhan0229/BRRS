# 새 N3(4212) 복구 및 RX 오류 진단 ON/OFF — 2026-09-04

## 결론

새 보드4212의 디버그 연결 장애는 APPROTECT 활성 상태에서 발생했다. 사용자 승인 후4212만 전체 삭제하고 N3 이미지를 프로그램해 복구했다. 이후 고정 배치에서 진단 OFF/ON 각3회, 총6000슈퍼프레임을 수집했다. **연결 문제는 해결됐지만 RF 고손실은 해결되지 않았다.**

N3의 평균 PER는 OFF36.20%, ON38.47%. 새 보드에도 고손실이 남아 있으므로 기존888의 단독 불량으로만 설명할 수 없다. 단, 이전888과 현재4212는 시간/재설치/펌웨어 조건이 구분된 데이터여서 보드 교체 개선량을 통제된 A/B 효과로 주장하지 않는다.

핵심 진단 성과는 오류 종류를 확인한 것이다. ON에서953개 오류 이벤트를 보존했고 RXFSL(DATA Reed–Solomon 복호 실패) 비트가707회 관측됐다. 상세 오류가 재시작 전에는 있었지만 그 이후 읽기에서는 대부분 사라져, 기존 `fint-only` 분류로 원인을 놓쳤다는 실측 근거를 확보했다. RF/채널/수신기 상태 중 어느 물리 메커니즘이 이 복호 실패를 만들었는지는 아직 미확정이다.

## 고정 조건과 데이터

NLOS6.9m 배치는 사용자 확인 기준. M32/PAC8/S3/G200/lead15µs, SB2000/SP2002µs, full `dwt_configure()`(fast switch OFF), polling+최적화SPI, IRQ/다른 프로파일링 OFF. 각1000SF, 동일 논리 순서234. INIT933는 로컬, N2=584/N3=4212/N4=818은 Air의 유전원 허브 구성으로 유지했다(전원/위치의 현장 관찰은 사용자 확인에 의존).

펌웨어 소스4f0c9be, 실행/검증 소스55a23fa. ON/OFF TX HEX·ELF는 동일하고 INIT 진단 플래그만 다르다. 각 실행에서 역할별 serial을 명시해 자동 정렬/회전을 사용하지 않았다. 동일 이미지로 매 실행 새로 플래시했다. raw/meta와 감독 프로세스의 console/status/명시적 명령을 보존했다.

| 시간 순서 | 진단 | N2 PER | N3 PER | N4 PER | 총 RX/3000 | 총 PER | 판정 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| r1 | OFF | 0% | 36.2% | 0.3% | 2635 | 12.167% | FAIL_PER |
| r2 | ON | 0% | 39.1% | 0.4% | 2605 | 13.167% | FAIL_PER |
| r3 | OFF | 0% | 34.2% | 0.1% | 2657 | 11.433% | FAIL_PER |
| r4 | ON | 0% | 37.2% | 0.2% | 2626 | 12.467% | FAIL_PER |
| r5 | OFF | 0% | 38.2% | 0.3% | 2615 | 12.833% | FAIL_PER |
| r6 | ON | 0% | 39.1% | 0.2% | 2607 | 13.100% | FAIL_PER |

집계는 [summary.json](summary.json)에 있다. OFF: RX7907/9000, PER12.144%, N3 RX1914/3000(PER36.2%). ON: RX7838/9000, PER12.911%, N3 RX1846/3000(PER38.4667%). N2는 총6000/6000, N4는5985/6000을 수신했다.

6회×3TX의 전체18건에서 beacon1000/1000, attempts1000, TX success1000, schedule PASS를 확인했다. 모든 raw에 END STATS가 있고 serial/firmware/raw hash·설정·소스·유효 수신/slot/host 수가 일치했다. deadline miss, delayed late, RDB mismatch/incomplete/resync/recovered, overrun, SPI error/timeout 등의 기록값은0이다. 진단 누락/queue overflow/hw faults도0. **이는 시스템 검사 통과이며, PER 기준5%는 모든 실행에서 불합격을 유지한다.** 논문의 최종 안정성 데이터와 섞지 않는다.

## ON에서 관측한 오류

| 오류 비트(pre-status) | r2 | r4 | r6 | 합계 | 설명 |
| --- | ---: | ---: | ---: | ---: | --- |
| RXFSL | 243 | 240 | 224 | 707 | DATA 부분 Reed–Solomon 정정 불가능 오류 |
| RXPHE | 40 | 43 | 58 | 141 | PHY header 정정 불가능 오류 |
| RXSTO | 40 | 24 | 27 | 91 | preamble 검출 이후 SFD timeout |
| RXFCE | 7 | 3 | 4 | 14 | DATA의 FCS/CRC 불일치 |

비트 카운트는 일반적으로 동시 발생을 허용하며 독립된 송신 패킷 수와 같지는 않다. RXPRD는953회, RXSFDD는862회였다. 진행 상태 비트에는 잔류 상태 가능성도 있으므로 모든 손실에서 preamble 검출이 반드시 성공했다고 단정하지 않는다. RXFTO(FWTO)/PTO/CIAERR/CPERR/ARFE는0. 단, Exp4 burst에서는 FWTO를 비활성화하므로 FWTO0만으로 미검출 손실을 제외할 수 없다.

로컬 DW3000 UM p96 및 SDK `deca_device_api.h`의 RXFSL 정의를 대조했다. RXFSL을 비컨 동기화 상실이나 preamble 미검출과 동일시하지 않는다. CIAERR0도 CIR 추정 품질이 충분하다는 보장은 아니다.

각 ON 실행은32개 burst RAM queue에 임시 저장한 뒤 모든 이벤트를 비트별로 집계했다. 원시 예시는 각 run 최초64개(총192개)이며, 이후 예시761개의 생략은 의도된 제한으로 queue overflow가 아니다.

## 읽기 시점과 슬롯 귀속의 주의점

실제 호출 순서는 pre SYS_STATUS → raw CMD_RX → SYS_TIME으로 slot 추정 → post SYS_STATUS/FINT → W1C clear이다. pre/post 사이에 W1C, driver reset, `dwt_forcetrxoff()`는 없으며 SPI recovery도0이었다.

953개 이벤트 중951회 rearm했다. 950개는 post48bit가 모두0, 1개는 RXPREJ만 관측됐고, rearm하지 않은2개는 상세 상태가 남았다. 이는 **이 설정의 RX 재시작을 포함한 구간에서 상세 상태가 사라지는 현상**의 실측이며, 모든 CMD_RX 동작에 적용되는 일반 규칙이나 매뉴얼 보장으로 주장하지 않는다.

r2 최초64개 예시에서는 pre의 logical_slot=1이64개 모두였지만 post 시각의 estimated_slot=2는49개였다. FSL/FCE처럼 늦게 확정되는 오류일수록 후속 슬롯으로 추정이 이동했다. 따라서 ON의 N4 오류 카운터 증가를 N4 송신 고장으로 해석하지 않는다. 실패 패킷의 source를 복호한 것도 아니므로 raw49개를 확정된 N3 패킷으로 재분류하거나 전체 이벤트로 외삽하지 않는다. 위 PER는 TX 예정 수와 유효 source별 RX 수로 계산하며 이 오류 귀속을 사용하지 않았다.

## 계측 영향은 미확정

ON의 pre-status 읽기는23µs, post-FINT는21µs다. 추가 읽기에는 실제 시간 비용이 있다. N3의 인접 ON−OFF 차이는 +2.9/+3.0/+0.9 percentage points, 평균+2.27pp. 3회·OFF 선행의 고정 순서이므로 시간 변동과 계측의 인과 효과를 분리한 동등성 증명이 아니다. **시스템 오류0을 근거로 무간섭/비회귀 PASS라고 판정하지 않는다.**

## 보존 상태와 다음 판단

공식 감사 JSON: off_r1, on_r2, off_r3, on_r4, off_r5, **on_r6.complete**. `on_r6.audit.json`은 복사 완료 전에 감사해 N4 metadata 미도착으로 FAIL_SYSTEM이 된 조기 감사이며 그대로 보존했다. 복사 완료 후 동일 raw를 다시 감사한 결과가 `on_r6.complete.audit.json`이다. 실제 RF 시스템 오류가 발생했다는 의미는 아니다.

양쪽 수집 프로세스는 종료됐다. 최종 배치는4212=N3 그대로이며 INIT는 진단 ON, TX는 동일 N2/N3/N4 이미지다. 자동 재실험은 없다. 실행 중 통행/환경 변화 보고는 받지 않았지만 현장을 직접 감시한 것은 아니다. 소스 변경/push는 없었다. 원시 로그는 `logs/exp4_nlos_rxerr_new4212_..._spiopt`와 `..._spiopt_rxerrdiag`에 분리해 보존했다.

다음에 필요한 대조군은 **같은4212·같은 자리의 M256 대조**와 **M32에서 N3를 첫 슬롯에 둔 순서 대조(진단 설정 일치)**다. 짧은 preamble의 수신 여유 부족과 선행 수신/재시작 이력 의존성을 분리한다. 이후888을 복귀시킨 같은 조건의 진단 비교가 필요하다. 현 단계에서는 B/C 포화·G150·6TX 최종 성능 실험으로 진행하지 않는다.
