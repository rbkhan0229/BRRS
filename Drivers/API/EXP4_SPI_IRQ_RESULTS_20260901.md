# Experiment 4 SPI/IRQ 최적화 결과

측정일: 2026-08-31~2026-09-01 (KST)

## 결론

현재 하드웨어와 펌웨어에서는 **polling + 최적화 SPI**가 기본 구현으로 가장 타당하다.

- 기존 SPI를 최적화 SPI로 바꾸면 기본 S3 hot path 평균이 269.1 us에서 194.7 us로 27.7% 줄고, 측정된 required guard 최악값은 192 us에서 89 us로 줄었다.
- 최적화 SPI 위에 IRQ를 추가하면 hot path 평균이 194.7 us에서 201.2 us로 오히려 3.3% 늘고, required guard도 89 us에서 99 us로 늘었다.
- 15-slot 포화 스케줄에서 polling + 최적화 SPI와 IRQ + 최적화 SPI는 모두 통과했다. 수신 PER은 각각 0.787%, 0.527%였지만 단일 포화 반복의 작은 차이를 IRQ의 링크 품질 개선으로 일반화하면 안 된다.
- IRQ + 기존 SPI는 기본 3-slot에서는 통과했지만 긴 버스트에서 처리 지연이 누적되었다. 5-slot은 통과하고 6-slot부터 실패했으므로, IRQ와 느린 SPI를 결합하는 것은 피해야 한다.
- BRRS의 슬롯별 scheduled delayed-RX와 비수신 구간 절전은 모든 측정에서 유지했다. DATA 구간 전체를 continuous RX로 바꾸지 않았다.

논문에서 방어 가능한 핵심은 “IRQ가 항상 polling보다 빠르다”가 아니라, **이 플랫폼에서는 SPI transaction 수명주기와 전체 foreground service time이 병목이며, IRQ만 추가하면 오히려 긴 버스트 처리량을 악화시킬 수 있다**는 것이다.

## 소프트웨어와 Git 안전성

- 비교 구현 커밋: `28b7ec499795f0c8be63434d5586ec9541b766a7`
- 작업 브랜치: `exp4-irq-spi-opt`
- 보존된 polling 최적화 브랜치: `exp4-polling-spi-opt` (`eae96e2ecef7cfaaa236dcdf5a53bb29da757a47`)
- `main`과 `origin/main`: 모두 `90cdffb623c65a1ace6f70e399d6f6913d21cef8`
- push: 수행하지 않음

IRQ 구현은 GPIO ISR에서 SPI를 사용하지 않는다. ISR은 이벤트 시각과 pending flag만 기록하고, foreground가 유일한 SPI 소유자다. RXFCG 뒤 해당 버퍼의 CIA 완료를 확인한 후 다음 RX를 재무장하며, IRQ와 polling 모두 같은 순서를 사용한다. SPI 최적화는 DATA 버스트 동안 SPIM만 활성 상태로 유지하고 CS는 transaction마다 토글한다. 실패·timeout·burst 종료 시 명시적으로 RX 상태와 SPI session을 정리한다.

멀티 보드 준비는 J-Link flash를 순차 실행한다. READY 이전의 순간적인 0 V/J-Link timeout만 최대 3회 재시도하며, 무선 실험이 시작된 뒤에는 자동 재시도하지 않는다. 실제로 `1050211584`의 일시적 0 V 오류가 setup 2/3에서 복구되었다.

## 공통 측정 조건

| 항목 | 값 |
|---|---|
| PHY / preamble | 동일 PHY, 32-symbol preamble, PAC8 |
| PSDU / application payload | 26 bytes / 16 bytes |
| 기본 스케줄 | S3, `234`, 노드당 1 slot/superframe |
| 공정 비교 guard / lead | G200 / 15 us |
| 반복 | 조합별 3회, 회당 1000 superframes, 총 9000 frames |
| 포화 스케줄 | `234234234234234`, 15 slots, 총 15000 frames |
| 슬롯 간격 | 297 us (airtime 97 us + guard 200 us) |
| 4500 us DATA 구간의 PHY 상한 | 15 slots |
| SPI clock / CPU clock | 32 MHz / 64 MHz |
| 빌드 | SEGGER Embedded Studio 8.28, Debug O0; direct SPI hot functions만 O3 |
| INIT probe | `1050270933` |
| sensor probes | `1050211584`, `1050273888`, `1050282818` (run마다 역할 회전) |

PASS는 유효 수신이 0보다 크고, deadline miss, RDB mismatch/incomplete, overrun, SPI 오류, timeout, IRQ pending/duplicate/spurious/arm failure가 모두 0일 때만 인정했다. PHY 오류와 누락 frame은 숨기지 않고 PER에 포함했다.

## 기본 S3 공정 비교

세 번의 반복을 합산했다. hot path 평균은 각 실행의 event-detect-to-buffer-free 평균을 수신 frame 수로 가중한 값이고, max와 percentile은 세 실행 중 최악값이다.

| 이벤트 | SPI | PASS | RX / expected | PER | hot avg | hot max | p99 / p999 | required guard 최악 | 시스템 결함 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| polling | 기존 | 3/3 | 8928 / 9000 | 0.800% | 269.1 us | 356 us | 282 / 282 us | 192 us | 0 |
| IRQ pending | 기존 | 3/3 | 8909 / 9000 | 1.011% | 311.3 us | 340 us | 336 / 338 us | 151 us | 0 |
| polling | 최적화 | 3/3 | 8948 / 9000 | 0.578% | 194.7 us | 205 us | 204 / 204 us | 89 us | 0 |
| IRQ pending | 최적화 | 3/3 | 8947 / 9000 | 0.589% | 201.2 us | 214 us | 213 / 213 us | 99 us | 0 |

polling + 기존 SPI의 세 번째 실행에서는 RDB의 CIADONE mirror가 없었지만, 다음 RX 재무장 전에 같은 frame의 global CIADONE이 확인된 경우가 1회 있었다. 이를 별도 `rdb_global_ciadone=1`로 기록했고 deadline miss나 buffer 오류는 발생하지 않았다. 나머지 11개 기본 실행에서는 해당 보조 완료가 0회였다.

PER 차이는 이 근거리 3회 측정만으로 통계적 우열을 주장하기에 충분하지 않다. 확실한 차이는 firmware service time과 포화 스케줄 통과 여부다.

## 4500 us DATA 구간 포화 및 실측 최대 슬롯

| 이벤트 | SPI | 최대 통과 슬롯 | RX / expected | PER | hot avg / max | required guard | 결과 |
|---|---|---:|---:|---:|---:|---:|---|
| polling | 기존 | 15 | 14872 / 15000 | 0.853% | 278.5 / 356 us | 192 us | PASS |
| IRQ pending | 기존 | 5 | 4924 / 5000 | 1.520% | 336.4 / 373 us | 192 us | PASS; 6 slots부터 FAIL |
| polling | 최적화 | 15 | 14882 / 15000 | 0.787% | 201.1 / 204 us | 89 us | PASS |
| IRQ pending | 최적화 | 15 | 14921 / 15000 | 0.527% | 209.7 / 214 us | 99 us | PASS |

IRQ + 기존 SPI 용량 경계:

| slots | RX / expected | required guard | deadline miss | mismatch | overrun | 판정 |
|---:|---:|---:|---:|---:|---:|---|
| 5 | 4924 / 5000 | 192 us | 0 | 0 | 0 | PASS |
| 6 | 4930 / 6000 | 203 us | 199 | 1 | 0 | FAIL |
| 7 | 5889 / 7000 | 220 us | 173 | 4 | 0 | FAIL |
| 15 | 12852 / 15000 | 220 us | 318 | 2 | 3 | FAIL |

IRQ + 기존 SPI의 IRQ 자체는 모든 실행에서 `events == dispatches`, pending/duplicate/spurious/arm failure 0으로 정상이다. 실패 원인은 ISR 내 SPI 충돌이 아니라 foreground hot path가 297 us 슬롯 간격보다 길어져 다음 이벤트 처리가 누적 지연된 것이다.

## Guard 경계 해석

- polling + 최적화 SPI의 이전 polling 전용 revision에서는 G150이 3/3 통과했고 G100이 한 번 통과 후 overrun/mismatch로 실패했다. 현재 rev44의 G200 공정 비교에서는 required guard 최악값이 89 us였지만, CIA-safe 재무장 순서가 바뀌었으므로 G150을 논문 기본값으로 확정하려면 rev44에서 3회 재검증하는 편이 안전하다.
- IRQ + 최적화 SPI는 IRQ 동작이 같은 rev43에서 G175가 3/3 통과했고 G150은 required guard 155 us, deadline miss 2로 실패했다. 따라서 현재 검증된 IRQ 기본 guard는 G175다.
- 네 조합을 모두 지원하는 공통 비교 guard로는 G200을 사용했다. polling + 기존 SPI의 최악 required guard가 192 us이므로 여유가 8 us뿐이다. 제품 기본값에 기존 SPI까지 포함하려면 G225 정도의 추가 안전 여유가 합리적이다.
- 논문 주 구현을 polling + 최적화 SPI로 정한다면 현재 보수적 기본값은 G200이며, rev44 G150 반복 재검증 후 G150으로 낮추는 순서가 안전하다.

## 재현 메타데이터

기본 공정 비교 INIT firmware SHA-256:

| 조합 | SHA-256 |
|---|---|
| polling + 기존 SPI | `bb0a55078f6832cc6fbdea1fa19acbb4fa9030d3c537ef6d9fd13375c2767ef2` |
| IRQ + 기존 SPI | `09a25169d709914e23b414d67d85475745edf544adf080c83a94f9c3ee3be2e0` |
| polling + 최적화 SPI | `ccb4b1ee3c8c0bde6d0a9a5d1f4052382e531777b36d7eba6f1021b282cada01` |
| IRQ + 최적화 SPI | `3beb9db9fde68a1955d8473b0fd98507a46aadeba6217571a5930b8e430193d4` |

각 `.meta.txt`에는 role, 보드 serial, git commit/branch/worktree, SDK와 compiler, 최적화, SPI/CPU clock, CS idle floor, firmware와 raw log hash, 실험 파라미터가 기록된다.

## 로그 위치

- 기본 polling + 기존 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_compare_poll_legacy_0m_g200_l15_pac8_20260831/`
- 기본 IRQ + 기존 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_compare_irq_legacy_0m_g200_l15_pac8_20260831_irq/`
- 기본 polling + 최적화 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_compare_poll_spiopt_0m_g200_l15_pac8_20260831_spiopt/`
- 기본 IRQ + 최적화 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_compare_irq_spiopt_0m_g200_l15_pac8_20260831_spiopt_irq/`
- 15-slot polling + 기존 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_saturated_poll_legacy_0m_g200_l15_pac8_seq234234234234234_20260901/`
- 15-slot IRQ + 기존 SPI 실패: `/Users/songchieon/Desktop/DWM3000/logs/exp4_saturated_irq_legacy_0m_g200_l15_pac8_seq234234234234234_20260901_irq/`
- IRQ + 기존 SPI 5/6/7-slot 경계: `/Users/songchieon/Desktop/DWM3000/logs/exp4_capacity_irq_legacy5_0m_g200_l15_pac8_seq23423_20260901_irq/`, `/Users/songchieon/Desktop/DWM3000/logs/exp4_capacity_irq_legacy6_0m_g200_l15_pac8_seq234234_20260901_irq/`, `/Users/songchieon/Desktop/DWM3000/logs/exp4_capacity_irq_legacy_0m_g200_l15_pac8_seq2342342_20260901_irq/`
- 15-slot polling + 최적화 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_saturated_poll_spiopt_0m_g200_l15_pac8_seq234234234234234_20260901_spiopt/`
- 15-slot IRQ + 최적화 SPI: `/Users/songchieon/Desktop/DWM3000/logs/exp4_saturated_irq_spiopt_0m_g200_l15_pac8_seq234234234234234_20260901_spiopt_irq/`
- 보존된 IRQ 설계 실패 로그: `/Users/songchieon/Desktop/DWM3000/logs/exp4_irq_spiopt_smoke_0m_g150_l15_pac8_20260831_spiopt_irq/`

## 남은 외부 재현 단계

동일 컴퓨터에서 sensor 역할 회전은 완료했다. 그러나 사용자가 모든 보드를 현재 Mac에 연결한 조건이므로, **동일 binary를 다른 노트북에서 실행하는 검증은 아직 수행하지 않았다**. 이 단계는 세 sensor 보드와 INIT 보드를 다른 노트북으로 물리적으로 옮긴 후, 위 SHA-256과 동일한 binary로 최소 3회 반복해야 한다. 다른 노트북 결과가 나오기 전에는 cross-machine 재현성을 완료했다고 주장하면 안 된다.

