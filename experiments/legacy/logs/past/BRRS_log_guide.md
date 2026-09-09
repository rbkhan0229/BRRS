# BRRS 로그 읽는 법 (v1.2 펌웨어 기준)

## 0. 시작 줄 (config)

```
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=317us LEAD=100us TAIL=0us PERIOD=10430us CIR=0
```

| 필드 | 의미 | 비고 |
|---|---|---|
| SYNC_PLEN / DATA_PLEN | SDK 내부 코드값 (3=32sym, 7=64, 15=128, 31=256, 63=512) | 심볼 수 아님. 괄호 안이 심볼 수 |
| PRE_US | 프리앰블 길이 모델값 (올림) | 32sym→33, 64→66, 128→131, 256→261 |
| SLOT | 슬롯 간격 | 32sym→717, 64→750, 128→815, 256→945 |
| RX_WIN | RX 창 전체 길이 = (PRE+SFD+LEAD) + 175 + TAIL | |
| LEAD / TAIL | margin 설정값 (µs) | **런 재현성의 핵심. 로그만 보고 설정 복원 가능** |

## 1. PER 줄

```
N2: rx=968 expected=1000 miss=32 PER=3.20% err=32
```

- miss = expected − rx. **miss가 어디서 왔는지가 중요**: 아래 timeout/error 줄과 대조.
- `miss ≈ timeouts` → 못 들음 (타이밍 문제)
- `miss ≈ errors` → 듣다가 깨짐 (신호 품질 문제)
- `miss ≈ delayed late` → 창을 아예 못 엶 (스케줄링 문제)

## 2. 실패 원인 세분화 줄

```
RX timeouts=0 (fwto=0 pto=0)  RX errors=32 (sfdto=15 phe=12 fce=0 fsl=5)  delayed late=0
```

| 카운터 | 발생 시점 | 의미 | 주요 원인 |
|---|---|---|---|
| **fwto** | 프리앰블 검출 전 | 창 끝까지 아무것도 못 잡음 | lead 부족(창이 늦게 열림), 상대가 안 보냄 |
| **pto** | — | preamble detect timeout. **우리는 미설정 → 항상 0이어야 정상** | 0 아니면 코드 이상 |
| **sfdto** | 프리앰블 검출 후 | SFD를 못 찾음 | 노이즈 오검출(lead 길수록↑), SNR 부족 |
| **phe** | SFD 통과 후 | PHY 헤더 깨짐 | SNR 부족 |
| **fce** | 프레임 끝 | CRC 불일치 | 근접 포화(0m 등), 간섭 |
| **fsl** | 데이터 중간 | Reed-Solomon 동기 상실 | SNR 부족 |
| **delayed late** | 창 열기 전 | 예약 시각이 이미 지나서 rxenable 실패 | lead가 너무 큼 (config 전환 시간 > 마감) |

**진단 패턴:**
- 전부 fwto → lead 스윕 필요 (타이밍 바이어스)
- sfdto 위주 → lead 줄여보기 (노이즈 노출 시간 축소)
- phe/fsl 위주 → 환경/SNR 문제. margin으로 못 고침
- delayed late 위주 → lead 줄이거나 SYNC_BUFFER 늘리기

## 3. accumCount 줄

```
N2: min=12 max=20 avg=12.0 / plen=32 (n=968)
```

**PLEN = (창 늦어서 놓친 심볼) + (검출 고정비용 ~20sym) + accumCount**

- avg ≈ PLEN − 20 → 창이 제때 열림 (정상). 32sym→~12, 64→~43, 128→~108, 256→~237
- avg가 기준보다 낮음 → 그 차이 = 창이 늦게 열린 µs 수 (바이어스 직접 측정!)
- min이 출렁임 → 타이밍 지터 존재
- accumCount는 SNR과 직결: 심볼 2배 = +3dB

## 4. RX-open to RMARKER 줄

```
N2: min=142us max=142us avg=142us
```

- 기대값 = **PRE_US + SFD_US + LEAD** (= RX_EARLY). 32sym lead100 → 33+9+100=142
- min=max=기대값 → 스케줄링 산수 완벽 (정상)
- 기대값과 다름 → SYNC 타임스탬프/오프셋 계산 버그
- min≠max → 도착 지터 발생 (원래 0이어야 함)

## 5. 시간 오프셋 3종 (어느 시계로 쟀는가)

| 줄 | 시계 | 측정 구간 | 정상 모습 |
|---|---|---|---|
| Latency | MCU (nRF) | 상대 TX 표기시각 → 내 수신 처리 | ±10µs 지터는 MCU 폴링 탓, 무시 |
| RX offset from SYNC | MCU | SYNC 기준 → 수신 처리 | 위와 동일 |
| **UWB RX offset from SYNC TX** | DW3000 칩 | SYNC RMARKER → DATA RMARKER | **min=max (µs 단위 완전 상수)여야 정상** |

- UWB RX offset 기대값 = SYNC_BUFFER + slot_idx×SLOT = 32sym→3717, 64→3750, 128→3815, 256→3945
- 이 값이 min≠max로 퍼지면 → delayed-TX 쪽 문제
- 기대값과 다르면 → 두 보드의 SLOT_INTERVAL 불일치 (한쪽만 플래시함!)

## 6. TX 노드 로그

```
My TX: success=1000 attempts=1000 delayed_late=0
SYNC loss: 1 timeouts  RX errors=0
```

- delayed_late > 0 → SYNC 수신 후 config 전환이 슬롯 시각을 못 맞춤
- SYNC loss 1은 시작 직후 정상. 지속 증가 → SYNC 자체가 안 들림
- 여기 RX errors는 SYNC(512sym) 수신 에러 → 0 아니면 근접 포화 의심

## 7. 주의사항

- **양쪽 보드 항상 같이 플래시** (SLOT_INTERVAL이 어긋나면 전멸)
- 32sym PER은 절벽 근처라 런마다 출렁임 → 점이 아니라 **여러 런의 분포**로 보고
- 0m(밀착) 실험 금지: 포화로 fce/err 오염됨. 0.5~1m 유지
- 통계는 수신 성공 프레임 기준(생존자 편향) — 실패 원인은 2번 줄로만 추적 가능
