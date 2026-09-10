# BRRS 실험 case 목록과 실행 방식

이 문서는 `brrs_vehicle_manifest.json`과 현재 자동 실행기의 실제 동작을 기준으로 한다.
아래의 `999 case`는 **현재 논문용 전수 profile을 끝까지 수행할 때의 수**다.
내일 차량에서 반드시 모두 실행해야 하는 횟수도 아니고, 연결된 보드 수를 곱한 수도 아니다.

## 1. case 한 개의 의미

case 한 개는 다음이 모두 고정된 한 번의 무선 실행이다.

- stage, PHY 조건, PAC, lead;
- 활성 물리 TX 집합과 논리 노드 배정;
- Exp4라면 물리 TX 수 `S`, 슬롯 수 `K`, guard, SB/SP, 슬롯 소유 순서;
- 반복 block 번호;
- 정확한 펌웨어 HEX/ELF와 source/manifest/조건 hash.

예를 들어 `paper_exp4_m32_pac8_l25_k13_s6_b01`은 M32, PAC8,
lead25 us, 물리 TX6대, DATA 슬롯13개, block1인 **한 case**다. 이 case에는
INIT 1대와 TX6대의 실행 job이 들어가지만 case 수는7이 아니라1이다.

### slot, superframe, case, block의 차이

| 용어 | 의미 | Exp4 M32/S6/K13 예시 |
|---|---|---|
| slot | TX 한 대가 보고서 한 개를 보낼 기회 | 한 슈퍼프레임 안에13개 |
| superframe | 비컨1회와 그 뒤의 DATA 슬롯 묶음 | 10 ms |
| case | PHY·노드·슬롯 설정을 고정한 한 번의 실행 | 1,000 superframes, 약10초 RF 측정 |
| block | 비교할 모든 조건을 각각 한 case씩 수행한 한 바퀴 | Exp4 전수 profile에서는58 cases |

따라서 block은 무선 프레임 구조나 일정한 시간 구간이 아니다. **반복 실험의 한 바퀴**를
뜻한다. 예를 들어 Exp1 block1은 M32/64/128/256 x PAC4/8의8조건을 한 번씩
측정한다. 그 뒤 block2에서 같은8조건을 다시 측정하되 순서를 바꾼다. 이렇게 하면
M32만5번 연속 측정한 뒤 M256을 측정하는 시간 편향을 피할 수 있다.

## 2. 현재 공통 조건

현재 차량 manifest가 표현하는 기본 조건은 다음과 같다. lead는 아직 차량에서
선정되지 않았으므로 아래 case ID의 lead17/25 같은 숫자는 형식 설명용 예시다.

| 항목 | 현재 값 |
|---|---|
| 슈퍼프레임 | 10,000 us |
| 비컨 프리앰블 | 기본 M512; Stage0~Exp5 직접 실행은 `--beacon-preamble`로 M32/64/128/256/512/1024 선택 가능 |
| DATA payload | application 16 B, 전체 PSDU 26 B |
| DATA 프리앰블 | M32, M64, M128, M256 |
| PAC | PAC4, PAC8 |
| Exp4 guard | 250 us |
| Exp4 SB/SP | 3000/2500 us |
| Exp4 수신 | 슬롯별 scheduled delayed-RX |
| Exp4 SPI | persistent-SPIM 최적화 ON |
| IRQ/fast PHY/profile | OFF |
| 목표 | 모든 물리 노드의 각 run PER < 1%, 시스템 오류 0 |

물리 보드 매핑은 다음과 같다.

| 역할 | J-Link serial | 계획 위치 |
|---|---|---|
| INIT/RX | 1050270933 | frunk |
| N2 | 1050211584 | bumper_A |
| N3 | 1050273888 | bumper_B |
| N4 | 1050282818 | driver_seat |
| N5 | 1050208509 | passenger_seat |
| N6 | 1050227627 | trunk_A |
| N7 | 1050204212 | trunk_B |

## 3. Stage0: PAC별 lead 지도 — 82 case

목적은 M32 delayed-RX에서 lead에 따른 획득/PER/오류 변화를 PAC별로 그리는 것이다.

- 링크: 물리 N4 `1050282818` -> INIT `1050270933` 단일 링크;
- M32, tail0;
- PAC4와 PAC8;
- lead 0~40 us의 모든 정수 41개;
- lead당 2,000 전송 기회;
- 전체 grid는 조건당1회.

실행 순서는 고정 순차 증가가 아니라 다음 순열이다.

`20, 0, 40, 10, 30, 5, 25, 15, 35, 2, 22, 12, 32, 7, 27, 17, 37, 4, 24, 14, 34, 9, 29, 19, 39, 1, 21, 11, 31, 6, 26, 16, 36, 3, 23, 13, 33, 8, 28, 18, 38`

계산은 `41 leads x 2 PAC = 82 case`다. ID 예시는
`paper_stage0_m32_pac4_l20_b01`이다.

Stage0은 단일 링크 측정이다. 따라서 여기서 고른 lead를6링크 전체의 검증값으로
바로 동결하면 안 된다. 최종 후보는 Exp4 S6/K6에서 모든 물리 링크로 다시 확인한다.
이 S6 lead 확인은 현재999개 전수 행렬에 별도 항목으로 포함되어 있지 않은
차량 캘리브레이션이다.

## 4. Stage0 선정 lead 확인 — 10 case

전체 grid 뒤 PAC4 후보1개와 PAC8 후보1개를 고른다.

- `lead-1`, `lead+1` 통과는 필수 조건이 아니다;
- 연속 통과 구간이 있으면 넓은 구간의 중앙을 우선한다;
- 고립된 성공점도 후보가 될 수 있으나 반복 결과가 불안정하면 동결하지 않는다;
- 선정 lead 자체를 PAC별5회 반복한다;
- 모든 run의 PER가1% 미만이고 pooled Wilson95 상한도1% 미만이어야 한다.

계산은 `2 PAC x 1 selected lead x 5 blocks = 10 case`다. ID 예시는
`paper_stage0_m32_pac8_l25_confirmation_b03`이다. 같은 값을5회 연속 소진하지
않고 PAC4/PAC8 조건을 한 번씩 수행한 뒤 다음 confirmation block으로 넘어간다.

## 5. Exp1: 프리앰블별 PER — 40 case

목적은 같은 단일 링크에서 DATA 프리앰블 축소가 신뢰성에 미치는 영향을 비교하는 것이다.

- 링크: 물리 N4 -> INIT;
- M32, M64, M128, M256;
- PAC4는 선정 PAC4 lead, PAC8은 선정 PAC8 lead;
- 조건당 2,000 전송 기회;
- 논문 profile 5 blocks.

block당 조합은 다음8개다.

| PAC | DATA 프리앰블 |
|---|---|
| 4 | 32, 64, 128, 256 |
| 8 | 32, 64, 128, 256 |

계산은 `4 M x 2 PAC x 5 blocks = 40 case`다. ID 예시는
`paper_exp1_m32_pac8_l25_b01`이다.

## 6. Exp2: 링크별 CIR 품질 — 144 case

목적은 축소 프리앰블에서 CIR 진단값, 누산 심볼, first-path 관련 품질과 PER를
각 차량 위치 링크별로 측정하는 것이다.

- 한 case에는 INIT와 물리 TX 한 대만 활성;
- N2, N3, N4, N5, N6, N7을 순서대로 선택;
- 활성 물리 TX는 단일 링크 펌웨어의 논리 N2 역할을 맡음;
- 나머지 TX5대는 halt 상태를 확인;
- M32, M64, M128, M256;
- PAC4와 PAC8 및 각 PAC의 선정 lead;
- 조건당1,000 전송 기회와 성공 프레임별 CIR 요약;
- 논문 profile 3 blocks.

block당 `4 M x 2 PAC x 6 physical links = 48 case`, 전체는
`48 x 3 = 144 case`다. ID 예시는
`paper_exp2_m32_pac8_l25_txN3_b02`다. 여기서 `txN3`은 실제 물리 위치/serial이며,
무선 프레임의 논리 source는 N2다.

## 7. Exp3: SFD/PHR airtime 분해 — 9 case

목적은 PHY 구성요소별 airtime과 수신 성공을 비교하는 것이다.

- 링크: 물리 N4 -> INIT;
- M32, PAC8 선정 lead;
- A: SFD8 + standard-rate PHR;
- B: SFD16 + standard-rate PHR;
- C: SFD8 + data-rate PHR;
- 조건당1,000회 EXTTXE/수신 측정;
- 논문 profile 3 blocks.

계산은 `3 variants x 3 blocks = 9 case`다. ID 예시는
`paper_exp3_m32_pac8_l25_A_b01`이다.

## 8. Exp4: 다중 노드와 포화 용량 — 696 case

목적은 물리 TX 수 확장성과 10 ms 슈퍼프레임 안의 보고서 처리 용량을 검증하는 것이다.
모든 case는1,000 슈퍼프레임이다.

### S1~S6를 나누는 이유

`S`는 슬롯 수가 아니라 **동시에 활성화한 실제 TX 보드 수**다.

- S1: 단일 링크 기준선을 만든다;
- S2~S5: 실제 송신기가 늘어날 때 어느 단계부터 손실·후반 슬롯 문제·수신 재무장
  문제가 나타나는지 찾는다;
- S6: 최종 목표인 실제 TX6대를 검증한다;
- S6에서 K를6보다 크게 바꾸는 조건: 물리 노드 수는6대로 유지하면서 같은 보드가
  여러 슬롯을 사용하여 처리량 한계를 찾는다.

예를 들어 S6/K13은 실제 TX가6대이고 보고 기회가13개다. 반대로 S3/K3은
실제 TX3대가 각각 한 번씩 전송한다. S1~S6 분리는 “독립 차량 노드를 몇 대까지
동시에 운용했는가”를 검증하고, K 증가는 “그 노드들이 한 주기 안에 보고서를 몇 개까지
처리할 수 있는가”를 검증한다.

S2~S5가 실패 원인의 증가 지점을 보여준다는 장점은 있지만, 모든 M/PAC 조합을
12 blocks 반복하는 것은 매우 큰 전수 계획이다. 논문의 핵심이 S6 신뢰성과 용량이라면
S2~S5는 대표 PHY 조건의 확장성 곡선만 남기고 줄일 수 있다. 이는 실제 데이터 수집 전에
축소 원칙을 확정해야 하며, 결과를 본 뒤 좋은 조건만 남기는 방식으로 줄이면 안 된다.

### S1~S5: block당 40 case

S는 동시에 설치·활성화한 물리 TX 수다. S1~S5에서는 노드마다 한 슬롯씩 사용하므로
슬롯 수 K는 S와 같다.

| 물리 TX 수 | 슬롯 수 | block당 조건 |
|---:|---:|---:|
| S1 | K1 | 4 M x 2 PAC = 8 |
| S2 | K2 | 4 M x 2 PAC = 8 |
| S3 | K3 | 4 M x 2 PAC = 8 |
| S4 | K4 | 4 M x 2 PAC = 8 |
| S5 | K5 | 4 M x 2 PAC = 8 |

합계는 `5 S levels x 4 M x 2 PAC = 40 case/block`이다. S1은 항상 물리 N4가
논리 N2를 맡는다. S2~S5는 block마다 설치 순서를 한 칸씩 회전하여6 blocks 동안
보드/위치와 논리 슬롯 역할의 영향을 분리한다.

### S6: block당 18 case

S6에서는 여섯 물리 TX가 모두 활성이고, 공통6슬롯과 PHY별 포화 후보를 비교한다.

| M | 검사 슬롯 K | PAC4 case | PAC8 case | 합계 |
|---:|---|---:|---:|---:|
| 32 | 6, 12, 13 | 3 | 3 | 6 |
| 64 | 6, 12 | 2 | 2 | 4 |
| 128 | 6, 10 | 2 | 2 | 4 |
| 256 | 6, 8 | 2 | 2 | 4 |

합계는 `18 case/block`이다. 예를 들어 K13은 여섯 독립 물리 노드가13대라는
뜻이 아니라, 여섯 보드가 `2345672345673` 순서로 한 슈퍼프레임에 총13개
보고 기회를 나눠 갖는 조건이다.

### 반복과 역할 회전

한 block은 `S1~S5 40 + S6 18 = 58 case`다. Exp4의12 blocks는
6회 역할 회전을2주기 수행하므로 `58 x 12 = 696 case`가 된다.

- block1: 원래 설치 매핑;
- block2~6: 논리 역할을 한 칸씩 회전;
- block7~12: 같은6회전의 두 번째 주기;
- 각 block 안에서는58조건을 한 번씩 수행한 뒤 다음 block으로 이동;
- 조건 순서는 block마다 회전하고 짝수 block에서는 역순으로 실행.

## 9. Exp5: 차량 링크별 원시 CIR — 18 case

목적은 같은 PHY로 각 차량 위치 링크의 원시 CIR/PDP 구조를 기록하는 것이다.

- M1024, PAC32;
- lead는 PAC8 Stage0 선정값을 전달하지만 PAC32 최적값이라는 뜻은 아님;
- N2~N7을 한 대씩 활성화하고 나머지5대는 halt;
- 링크당1,000 전송 기회;
- 성공 프레임 중 최대30프레임에 대해 프레임당300 raw CIR samples 저장;
- 논문 profile 3 blocks.

계산은 `6 physical links x 3 blocks = 18 case`다. ID 예시는
`paper_exp5_m1024_pac32_l25_txN6_b03`이다.

## 10. 999 case 계산

| 구분 | 계산 | case |
|---|---:|---:|
| Stage0 grid | 41 lead x 2 PAC | 82 |
| Stage0 confirmation | 2 PAC x 1 lead x 5 | 10 |
| Exp1 | 4 M x 2 PAC x 5 | 40 |
| Exp2 | 4 M x 2 PAC x 6 links x 3 | 144 |
| Exp3 | 3 variants x 3 | 9 |
| Exp4 | 58 conditions x 12 blocks | 696 |
| Exp5 | 6 links x 3 | 18 |
| **합계** |  | **999** |

다음은999에 포함되지 않는다.

- 차량 장착 직후 M256/K6 및 M32/K6의6링크 사전 점검;
- Stage0 후보의 Exp4 S6/K6 네트워크 확인;
- SB 후보 탐색, SP 후보 탐색, guard 후보 탐색;
- 사람 통행, 케이블 접촉, 수집 실패 등 명시적으로 오염된 case의 대체 측정;
- 현재 manifest에 없는 PHY/비컨 조건(Exp4 직접 탐색에서는 CLI로 실행 가능).

따라서999를 내일의 실행 횟수로 사용하면 안 된다. 캘리브레이션 뒤 시간 예산에 맞는
핵심 검증 block을 사전 확정해야 한다.

## 11. 반복 실행 원칙

같은 조건을 필요한 횟수만큼 연속 실행하지 않는다. 각 stage에서 block1의 모든 조건을
한 번씩 수행하고 block2로 돌아간다. 이 방식은 시간, 차량 온도, 배터리/USB 상태,
주변 통행이 특정 조건에만 몰리는 것을 줄인다.

다만 Stage0, lead 네트워크 확인, SB/SP/guard/최대 K 탐색은 뒤 실험의 설정을 결정하므로
Exp1~Exp5와 섞지 않는다. 순서는 다음과 같다.

1. 6링크 사전 점검;
2. Stage0 및6링크 lead 확인;
3. SB -> SP -> guard를 한 축씩 탐색하고, 매 결과에서 다음 K를 다시 계산하여 최대 K 확인;
4. 최종 manifest/펌웨어 동결;
5. Exp1~Exp5를 stage별 block 방식으로 반복.

사람 통행이 보고된 case는 raw를 보존하고 제외 이유를 기록한다. 같은 조건을 즉시
재실행하여 좋은 결과를 고르지 않고, 현재 block의 다른 조건을 마친 뒤 대체 case를
별도 bundle에 수집한다.

## 12. 자동 실행기와 빠른 탐색의 역할

실험 절차는 목적에 따라 세 단계로 나눈다.

| 모드 | 용도 | 실행 방법 | 결과의 지위 |
|---|---|---|---|
| quick exploration | 즉흥적인 아이디어 한두 번 확인 | 기존 저수준 capture/batch 명령과 CLI 옵션 | 진단 자료, 논문 통계에 바로 합산하지 않음 |
| screening | lead/SB/SP/guard/K 후보를 한 번씩 비교 | 저수준 실행기로 한 case씩 적응 실행하고 raw log 보존 | 최종 후보 선정 자료 |
| paper validation | 확정 조건의 반복·회전 검증 | 동결 manifest와 immutable case bundle | 논문용 공식 증거 |

따라서 “궁금한 조건 하나를 한 번 확인”할 때마다 완전한 paper manifest를 만들 필요는 없다.
이미 지원되는 옵션이면 내가 저수준 실행기로 한 case만 빌드·플래시·수집·검증하면 된다.
이때도 최소한 보드 serial 확인, 고유 로그 경로, 기존 로그 비덮어쓰기, 양쪽 설정 일치,
raw 로그 보존은 유지한다. 이 정도는 결과 혼동과 잘못된 보드 플래시를 막기 위한 최소 절차다.

차량 현장의 SB/SP/guard/K 탐색은 앞 결과가 다음 값을 결정하므로 고정 manifest를 미리
만들지 않는다. 내가 `brrs_exp4_capture.sh`와 `brrs_exp4_multi_tx.sh`의 CLI 인자를
case마다 명시하여 직접 실행한다. 각 실행 뒤 timing margin, 시스템 오류, 보드별 PER를
보고 다음 값과 슬롯 sequence를 정한다. `--force`로 좋은 결과만 덮어쓰지 않고 run 번호와
고유 로그 디렉터리를 사용한다.

탐색 결과가 의미 있어 반복하거나 논문에 사용할 가능성이 생기면 그때 manifest 조건으로
승격한다. 즉 **먼저 빠르게 탐색하고, 채택할 때 엄격하게 동결**한다.

공식 데이터는 기본적으로 다음 자동 경로로 수행한다.

1. `brrs_suite_manifest.py`가 manifest로부터 case ID, 역할, 조건, 명령을 결정한다.
2. `brrs_suite_campaign.py prepare`가 정확한 펌웨어를 빌드하고 immutable case bundle과 hash를 만든다.
3. `brrs_suite_case.py`가 보드 serial 확인, 비참여 보드 halt, TX READY, RX 시작, 수집과 flash readback을 조정한다.
4. 각 stage의 `brrs_*_capture.sh`가 실제 build/flash/RTT capture/verifier를 실행한다.
5. 결과 도구가 누락, 역할/hash 불일치, 시스템 오류, 노드별 PER를 fail-closed로 판정한다.

요청한 값이 이미 지원되는 옵션이면 실행기를 수정하지 않는다. lead, DATA M32/64/128/256,
PAC4/8, guard, SB, SP, cycles, 슬롯 수/sequence, slotted-RX, SPI 최적화 등은 기존
CLI 인자만 바꿔 바로 실행할 수 있다. 여러 번 반복하거나 공식화할 때 manifest 값으로
새 case를 만든다.

비컨 프리앰블은 이제 Stage0~Exp5의 직접 실행기와 통합 실행기에서 정식
`--beacon-preamble` 옵션이다. 기본값은 M512이고 M32/64/128/256/512/1024를 받을 수
있다. INIT와 sensor에 같은 compile-time 값을 전달하고, 비컨 airtime/RX window 계산도
그 값으로 다시 계산한다. 비기본 값은 `_sync<M>` 로그 경로로 격리되고 각 정적 이미지의
build stamp가 `--no-build` 오사용을 막는다. `EXP_LOG_CONFIG_CSV`,
`BRRS_BEACON_*_CSV` 및 각 verifier가 양쪽의 실제 값을 확인한다. Exp4는 조건별 이미지
cache도 `_sync<M>`로 분리한다. 예시는 다음과 같다.

```bash
./brrs_exp4_capture.sh init 256 6 1 vehicle 0 \
  --beacon-preamble 256 --sync-buffer 1703 --sync-prep 2002 \
  --guard 200 --lead 15 --pac 8 --spi-opt --phy-fast-switch
```

현재 paper manifest의 공식 비컨 값은 여전히 M512다. 탐색에서 다른 값이 채택되기 전에는
manifest case를 바꾸거나 공식 결과와 합산하지 않는다.

요청한 값이 현재 case schema와 빌드 옵션에 없으면 단순히 명령 한 줄만 바꾸지 않는다.
새 축을 추가할 때는 다음을 함께 수정한다.

- INIT/sensor의 공통 비컨 PHY 설정;
- frame airtime과 RX window/timing 계산;
- build/capture CLI 옵션과 이미지 cache 경로;
- manifest 조건, case ID와 hash;
- 부팅/config 로그와 verifier;
- 양쪽 보드가 같은 비컨 PHY를 쓰는지 확인하는 테스트.

즉 현재 방식은 “항상 소스를 임시 수정해 명령을 하나씩 실행”하는 방식도 아니고,
“모든 즉흥 실험에 논문용 절차를 강제”하는 방식도 아니다. **기존 옵션은 빠른 직접 실행,
새로운 축은 격리된 탐색, 채택된 조건은 manifest-driven 자동 실행**으로 구분한다.
