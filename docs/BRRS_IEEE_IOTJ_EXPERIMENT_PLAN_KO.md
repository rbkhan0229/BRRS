# BRRS IEEE IoT Journal 제출용 실험 실행 계획

작성일: 2026-08-07

상세 설계와 통계 기준은 `BRRS_IEEE_IOTJ_EXPERIMENT_PLAN.md`를 기준으로 하고,
이 문서는 실제 재실험 순서와 판단 기준을 빠르게 확인하기 위한 한국어 실행본이다.

## 1. 논문의 중심 주장

차량 외부의 소형 센서 노드가 짧은 데이터를 차량 내부 코디네이터에 주기적으로
전송하는 UWB 네트워크를 대상으로 한다. 표준 256-symbol 비컨으로 네트워크를
동기화하고, 비컨이 알린 슬롯 시각에 delayed-TX와 delayed-RX를 수행한다.

검증할 핵심은 다음 세 가지다.

1. 비컨 기반 예약 수신에서 DATA 프리앰블을 256에서 32 또는 64 symbol로 줄여도
   정해진 신뢰도 기준을 만족할 수 있는가?
2. 프리앰블 축소가 실제 DATA airtime, 한 슈퍼프레임의 수용 노드 수, 집합
   goodput을 얼마나 개선하는가?
3. 이 결과가 통제된 LOS/NLOS와 실제 차량 배치에서도 같은 방향으로 나타나는가?

DWM3000에서는 SFD와 PHR을 실제로 제거하지 못한다. 따라서 논문에서는
"완전한 BRRS PHY를 구현했다"고 주장하지 않고, 표준 호환 프로토타입의 실측값과
실험 3에서 얻은 SFD/PHR 시간을 이용한 이상적 BRRS 재구성값을 구분한다.

## 2. 현재 비컨과 스케줄 구조

비컨은 다음 값을 직접 전달한다.

- DATA 프리앰블 길이 `M`;
- DATA PSDU 바이트 수;
- DATA rate;
- `active_node_bitmap`;
- 비컨 RMARKER에서 첫 DATA RMARKER까지의 시간;
- DATA 슬롯 간격;
- 슈퍼프레임 주기;
- 슈퍼프레임 순번.
- DATA 슬롯 수와 슬롯별 송신 노드 순서.

제출용 펌웨어는 v2.0, beacon protocol v3를 사용한다. DATA는 슬롯 오프셋을
다시 싣지 않고 슈퍼프레임 순번만 되돌려 보낸다. 코디네이터는 DATA RX RMARKER로
실제 슬롯을 계산하고 비컨의 슬롯별 송신 노드와 source ID를 대조한다. 이에 따라
DATA 프로토콜 헤더는 12바이트에서 8바이트로 줄었다.

`active_node_bitmap`에서 N2~N8은 각각 bit 0~6에 대응한다. 활성 노드는 ID 오름차순으로
빈 슬롯 없이 배치된다. 예를 들어 `0x0D`이면 N2, N4, N5가 슬롯 0, 1, 2를 사용한다.

장비가 부족한 예비 시험에서는 같은 물리 노드를 반복 배정할 수 있다. 예를 들어
N2/N3를 각각 네 번 배정하면 `23232323`이 된다. 이 결과는 8개 전송 슬롯 처리량과
코디네이터의 연속 슬롯 처리 능력을 검증하지만, 독립 클럭을 가진 8개 노드 수용량을
실측한 것으로 표현하지 않는다.

실험 번호, priority, A/B/C와 같은 실험 편의 정보는 비컨에 넣지 않는다. 실험 3의
SFD 종류와 PHR rate는 송수신 펌웨어의 실험용 빌드 설정으로만 맞춘다.

## 3. 제출 데이터 수집 전 필수 조건

1. INIT와 모든 NORMAL 보드에 동일한 v2.0, beacon protocol v3 펌웨어를 사용한다.
2. 2노드 smoke test에서 INIT의 `BRRS_BEACON_CONFIG_CSV`와 NORMAL의
   `BRRS_BEACON_RX_CSV` 값이 일치해야 한다.
3. `BRRS_BEACON_REJECT=0`, `delayed_late=0`, wrong-slot=0,
   wrong-superframe=0을 확인한다.
4. N2/N4/N5처럼 연속되지 않은 비트맵을 한 번 시험하여 실제 슬롯 압축을 확인한다.
5. 보드 고정 지그, 거리, 높이, 안테나 방향, TX power, 채널, PAC, 데이터율을
   확정한 뒤 펌웨어와 분석 스크립트를 동결한다.
6. 이후 코드를 바꾸면 영향을 받는 조건은 다시 측정한다.

## 4. 공통 실험 원칙

- 채널 9, PRF 64 MHz, PAC8, STS off, DATA 6.81 Mbps를 기본값으로 고정한다.
- 비컨 프리앰블은 256 symbol로 고정한다.
- 재전송과 ACK는 사용하지 않는다.
- 한 활성 노드는 한 슈퍼프레임에 DATA 한 번을 전송한다.
- 각 조건은 독립 run 5회 이상 수행한다.
- 본 실험 1·2는 run당 DATA 2,000회, 실험 4는 조건당 1,000 superframe 10회를 권장한다.
- 프리앰블 조건의 실행 순서를 무작위로 섞는다.
- 최소 세 보드 pair를 사용하거나 보드 역할을 교대하고, 핵심 조건은 다른 날짜에
  반복한다.
- 원시 로그는 수정하지 않고 분석 결과를 별도 폴더에 만든다.

`PER=0%` 한 번을 완전 신뢰성의 증거로 쓰지 않는다. run을 독립 반복 단위로 삼고
95% 신뢰구간을 함께 보고한다.

## 5. Stage 0: lead/tail 고정

목적은 본 실험에서 사용할 lead margin을 사전에 결정하는 것이다.

- 조건: M32, PAC8, 1 m LOS, 고정 지그;
- lead: 0, 2, 4, 6, 8, 10, 12, 14, 15, 16, 20 us;
- tail 비교: `(0,0)`, `(선택 lead,0)`, `(0,100)`, `(선택 lead,100)`;
- 각 조건 2,000 frame x 5 run;
- 전이 구간은 통제된 NLOS에서도 반복.

PER뿐 아니라 FWTO, PTO, SFDTO, PHE, FCE, FSL과 성공 프레임의 accumCount 분포를
함께 기록한다. 최종 lead는 LOS 한 지점에서 PER이 가장 낮은 값이 아니라, LOS와
통제 NLOS 모두에서 사전에 정한 신뢰도 기준을 만족하는 가장 작은 안정값으로 정한다.

## 6. 실험 1: 최소 신뢰 프리앰블

| 변수 | 설정 |
|---|---|
| DATA 프리앰블 | 32, 64, 128, 256 symbol |
| 거리 | 0.5, 1, 2, 3, 5 m |
| 환경 | 통제 LOS, 통제 NLOS |
| 반복 | 조건당 2,000 frame x 5 run |

NLOS는 사람이나 빨래건조대처럼 변하는 물체 대신 위치와 재질을 고정한 금속판 또는
파티션을 사용한다. 각 run의 PER, 오류 원인, beacon loss, delayed scheduling failure를
분리한다. 결론은 "M32가 항상 가능"이 아니라 거리·환경별 신뢰도와 airtime의
trade-off로 제시한다.

## 7. 실험 2: CIR 및 first-path SNR

실험 1과 같은 지그에서 축소된 조건을 사용한다.

- M32, 64, 128, 256;
- 거리 1 m와 3 m;
- LOS와 통제 NLOS;
- 조건당 2,000 attempt x 5 run.

주 지표는 다음과 같다.

`FP_SNR_dB = 10 log10(fp_snr_ratio_x1000 / 1000)`

`RSSI=-128 dBm`은 현재 진단 경로에서 유효하지 않으므로 주 결과로 사용하지 않는다.
CIR은 수신 성공 프레임에서만 얻어지므로 표본 수와 PER을 항상 같이 표기한다. 프리앰블
두 배당 상대 이득이 이론적 3.01 dB 증가와 얼마나 일치하는지 회귀와 신뢰구간으로
평가한다.

## 8. 실험 3: SFD/PHR airtime

| 변형 | 설정 | 차분으로 얻는 값 |
|---|---|---|
| A | SFD8 + STD PHR | 기준 |
| B | SFD16 + STD PHR | B-A = SFD 8 symbol 추가 시간 |
| C | SFD8 + DTA PHR | A-C = STD와 DTA PHR 시간 차이 |

각 변형은 EXTTXE capture 1,000개 x 독립 reset 5회로 측정하며 `capture=TX success`여야
한다. 평균만 쓰지 않고 표준편차, 범위, 95% 신뢰구간을 보고한다.

가능하면 PSDU 30, 55, 56, 100, 127 B도 측정해 Reed-Solomon parity가 추가되는
경계의 계단형 airtime을 검증한다. 실험 3 결과는 표준 호환 실측 airtime에서 SFD/PHR을
뺀 이상적 BRRS 서브슬롯 시간을 재구성할 때 사용한다.

## 9. 실험 4: 다중 노드 수용량과 goodput

### 9.1 두 물리 센서 반복 슬롯 예비 시험

M32에서 다음 SES 구성을 사용한다.

- 코디네이터: `Exp4_32_S2x4_Init`;
- N2 센서: `Exp4_32_S2x4_N2`;
- N3 센서: `Exp4_32_S2x4_N3`.

비컨의 기대 스케줄은 `slot_count=8, slot_owners=23232323`이다. 1,000개
슈퍼프레임이 끝나면 INIT은 `expected=8000`, N2와 N3는 각각 `attempts=4000`이
정상이다. M256 비교에는 이름에서 `32`를 `256`으로 바꾼 세 구성을 사용한다.

이 모드는 8개 슬롯과 8개 DATA 전송을 실제 무선으로 수행한다. 다만 송신기 발진기,
안테나 위치, 채널이 두 개뿐이므로 결과 명칭은 `8-slot virtual-node load` 또는
`two-radio repeated-slot test`로 쓴다.

### 9.2 제출용 독립 노드 시험

먼저 현재 3,000 us인 beacon-to-first-slot buffer를 3000, 1500, 1000, 750, 500 us로
줄여 본다. 각 값에서 1,000 superframe 10회 동안 delayed-late가 0이고 link가 안정적인
가장 작은 값을 고정한다.

그다음 같은 비컨, payload, period, TX power, 위치에서 다음을 비교한다.

1. M256 표준 호환 TDMA;
2. M32 짧은 프리앰블 TDMA;
3. 필요하면 M64 안정성 절충점;
4. 실험 3의 SFD/PHR을 뺀 이상적 BRRS 분석값.

각 M에서 `active_node_bitmap`으로 N=1~7을 증가시킨다. 측정 수용량은 단순 슬롯 수식이
아니라 다음 조건을 모두 만족하는 최대 활성 노드 수다.

- 노드별·전체 PER 신뢰도 기준 충족;
- wrong-slot=0, wrong-superframe=0;
- delayed-late=0;
- beacon 설정 오류=0;
- 로그 완결성 통과.

보고 지표는 최대 신뢰 노드 수, 노드별 PER, 모든 노드가 성공한 슈퍼프레임 비율,
aggregate application goodput, offered load, 이용률, rearm slack이다.

## 10. 차량 적용 실험

코디네이터는 차량 실내 중앙의 고정 위치에 두고 센서는 앞 범퍼 좌·우, 뒤 범퍼/트렁크
등 실제 후보 위치에 고정한다. 차량 모델, 문과 창문 상태, 높이, 안테나 방향, 거리와
차폐 경로를 기록한다.

- M32, 64, 256;
- 위치별 2,000 frame x 5 run;
- 실험 1의 결론과 수용량 증가 방향이 차량에서도 유지되는지 평가.

한 차량 결과를 모든 차량에 일반화하지 않고 case study로 명시한다.

## 11. 핵심 네트워크 지표와 선택적 전력 실험

차량 전원을 사용하는 코디네이터에서는 에너지 절감을 핵심 기여로 주장하지 않는다.
다음 네트워크 지표를 주 결과로 사용한다.

- 정해진 신뢰도에서 슈퍼프레임당 최대 센서 보고 수;
- 고정 보고 주기에서 최대 수용 센서 수;
- 고정 센서 수에서 가능한 최대 보고율;
- aggregate application goodput과 채널 이용률;
- worst-case 전달 지연과 모든 슬롯 성공률;
- UWB airtime 감소에 따른 다른 UWB 서비스와의 공존 여유.

전력 측정은 주변 센서가 배터리나 에너지 하베스팅을 사용하거나 주차 중에도 동작하는
시나리오일 때만 보조 실험으로 수행한다. 이 경우 Power Profiler Kit II 또는 보정된
shunt/oscilloscope로 continuous/immediate RX와 beacon-scheduled delayed-RX를 비교한다.

현재 연구는 네트워크 논문으로 두고 ranging은 후속 연구로 분리한다. IoT-J 수준에서는
다중 노드 실측, 통제 NLOS, 차량 case study와 반복 통계가 있어야
"DW3000 설정 관찰"을 넘어 IoT 네트워크 기여로 설득하기 좋다.

## 12. 지금부터의 실행 순서

1. 양쪽 노트북에 v2.0/protocol-v3 소스를 맞추고 Exp4 M32/S1 smoke test를 한다.
2. 비컨 송수신 로그 값과 오류 카운터를 확인한다.
3. 비연속 bitmap 시험으로 참여 제어와 슬롯 압축을 검증한다.
4. Stage 0을 수행해 lead를 동결한다.
5. 실험 지그, NLOS 차폐물, 로그 manifest와 분석 스크립트를 동결한다.
6. 같은 물리 배치 블록에서 실험 1과 2를 연속 수집한다.
7. 실험 3 A/B/C와 PSDU 길이 sweep을 수집한다.
8. sync buffer와 superframe period를 확정하고 실험 4를 수행한다.
9. 차량 case study를 수행하고, 센서 전원 모델에 필요할 때만 전력을 측정한다.
10. 원시 데이터를 잠근 뒤 한 명령으로 표와 그림을 재생성하고 논문을 작성한다.
