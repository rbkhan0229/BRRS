# 과거 저손실 코드와 현재 RX 경로 비교 — 2026-09-04

## 결론과 범위

소스·Git 이력·과거 HEX hash·기존 로그만 검토했다. 추가 RF 실행, 플래시, 펌웨어 수정, commit/push는 하지 않았다. 단일 근본 원인은 아직 확정하지 못했다.

핵심 발견은 두 가지다. (1) 8/25 G250 저손실·시스템 PASS 실행의 네 역할 HEX가 모두 남아 있어 당시 바이너리 그대로 대조할 수 있다. (2) 현재 SPI OFF는 옛 코드가 아니다. RX 재시작/오류 처리의 공통 경로 변경이 남아 있어 기존 옵션 비교만으로 코드 회귀를 배제할 수 없다.

코드 비교 기준: 과거 후보 `90cdffb623c65a1ace6f70e399d6f6913d21cef8` → 현재 `55a23fa94fb7b4e372ec7b03d54be72f01b81e7a` (펌웨어 변경 `4f0c9be67f9bd903d7a55f13ce55673719208cbf`). 90cdffb는 과거 raw의 rev24/v2.11/PAC 및 경로와 일치하는 유력 후보이나 당시 commit/dirty 기록과 재현 빌드 hash가 없어 정확한 소스 provenance는 미확정이다. 999df79는 rev23이므로 8/25 코드라고 단정할 수 없다.

## 1. 정확한 과거 바이너리 확보

8/25 M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF:

- 전체 RX2981/3000, PER0.633%; N2 0%, N3(888)1.8%, N4 0.1%.
- TX 모두1000/1000, RDB mismatch/incomplete/resync/overrun0, collectionPASS.
- 8/25 G200 낮은 PER 기록도 사실이지만 시스템 오류가 있어, 동일 바이너리 대조군은 G250이 더 적절하다.

공통 경로: `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2/Drivers/API/Build_Platforms/nRF52840-DK/Output/Debug/Exe/exp4/`.

| 역할 | 상대 경로 | SHA256 |
| --- | --- | --- |
| INIT | plen32_sensors3_guard250/exp4_32_s3_init.hex | 0a020d4cef54c3ac81c248fdf4595c589e500073699750650ca44a59a0d2b737 |
| N2 | plen32_sensors3_guard250_lead11/exp4_32_s3_N2.hex | 9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20 |
| N3 | plen32_sensors3_guard250_lead11/exp4_32_s3_N3.hex | 9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651 |
| N4 | plen32_sensors3_guard250_lead11/exp4_32_s3_N4.hex | 3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1 |

TX 폴더명 lead11은 의미를 추정하지 않았다. 네 HEX 모두 실험 당시 metadata의 firmware_sha256와 실제 shasum이 일치한다. 보드에 다시 적용하기 전에는 시작 로그의 역할·PHY·스케줄도 재검증해야 한다.

대조 metadata: 프로젝트 루트 기준 `logs/exp4_nlos_6.9m_guard250_6.9m_g250_l15_pac8_20260825/exp4_32_s3_r1_init.meta.txt`와 `experiment_logs_20260825/TX_remote/exp4_nlos_6.9m_guard250_6.9m_g250_l15_pac8_20260825/exp4_32_s3_r1_n{2,3,4}.meta.txt`.

역할: INIT933/N2=584/N3=888/N4=818. 현재 N3는4212이므로 과거1.8% 재현을 보장하지 않는다. 신규 old/new 비교 쌍에서는 같은4212를 유지해야 보드 변경을 섞지 않는다. 과거888도 이후 필요하면 별도의 보드 교차 요인으로 다룬다.

## 2. 확인된 비변경 사항

현재 full-configure, PHYfast/profile OFF 기준:

- DATA channel9/code9, SYNC code10, 6.8Mb/s, PHR/SFD, STS OFF/PDOA0, M32/PAC8 설정 동일.
- TX power index40 및 linear TX 적용, payload26B/app16B, delayed-TX 프레임 생성/시각 계산에 감도 악화를 설명하는 상수 변경을 찾지 못했다.
- full `ull_configure()`의 SYS_CFG/OPS/PAC/DTUNE3/DGC/PGF 실동작은 동일. 추가 driver 코드 대부분은 꺼진 fast/profile 조건부 코드다.
- `--spi-opt`의 persistent/direct 정의는 INIT에만 적용된다. TX를 몰래 같은 최적화로 바꾼 옵션이 아니다.
- 과거에도 첫 슬롯만 scheduled delayed-RX, 이후는 제한된 DATA burst 내 CMD_RX 재시작이었다. 이 구조 자체가 최근 새로 생긴 것은 아니다.

현재 소스 근거: `Drivers/API/Src/examples/ex_35b_brrs_normal/brrs_normal.c:379,428,443,496,1286`; `Drivers/API/Shared/dwt_uwb_driver/dw3000/dw3000_device.c:1964,1994,2009,2014,2071,2113`; `Drivers/API/brrs_exp4_build.sh:248`.

이는 소스 분석이며 실제 RF 출력·아날로그 상태의 완전한 동등성을 측정한 결과는 아니다.

## 3. 실제 달라진 RX 순서

### 정상 패킷 후 재시작

과거 후보: RXFCG 감지 → CMD_RX → RDB/metadata/CIA 검증.

현재: RXFCG 감지 → RDB/CIA 완료 확인 → CMD_RX → metadata.

`28b7ec4`에서 polling/IRQ 공통으로 순서를 바꿨다. SPI OFF에도 이 순서는 남는다. 현재 brrs_init.c:5570,5587,5679,5770 참고.

기존 기록의 detect-to-RX-command는 min59/avg59.722/max178us, 현재 run8은 min=avg=max73us다. 총 hot path와 느린 꼬리는 줄었지만, 평소 good frame 뒤 다음 RX 재시작은 약14us 늦어졌다. 전체 처리시간 개선을 모든 구간의 단축으로 해석하면 안 된다.

이 변경은 timestamp/buffer 안전을 위한 이유가 있어 결함으로 확정하지 않았다. 단순히 CIA 대기를 제거하는 것은 제안하지 않는다. G400에서도 손실이 남았으므로 약14us의 부족만으로 고손실을 설명하는 가설은 약하다. 수신 진입 상태/이전 프레임 이력과 약한 경로의 상호작용을 분리할 필요가 있다.

### 오류 복구 및 진단

과거: 상세 상태 read → 슬롯 추정 → 상태 clear → CMD_RX.

현재 진단OFF: CMD_RX → 슬롯 추정/상세 read → 상태 clear. 진단ON은 CMD_RX 앞에 별도의48bit 상태 snapshot을 추가한다.

현재 brrs_init.c:6127–6172. 실제 pre/post 기록은 재시작 전 있었던 오류 상세가 뒤에서는 사라지는 것을 보여준다. 이는 fint-only 로그 문제를 설명하지만, 이미 pre snapshot에 존재한 RXFSL의 생성 원인 자체를 설명하지는 않는다.

### 새로 확인한 진단·제어 결합

`exp4_slot_from_event_time()`(brrs_init.c:2939)은 이벤트 순간 timestamp가 아니라 처리 도중의 현재 SYS_TIME을 읽어 nearest-slot을 계산한다. 이후 이 추정값을 `exp4_advance_after_event()`(:3301, 호출:6212)에 전달하여 실제 current_rx_slot도 진행시킨다. current_rx_slot은 다음 CMD_RX 발행 가능 여부(:3090)를 결정한다.

따라서 진단ON의 추가 read로 추정 슬롯이 변하면 로그 라벨만이 아니라 이후 제어도 영향을 받을 수 있다. 앞선 ON raw에서 logical_slot1/estimated_slot2로 이동한 샘플들이 관측됐다. ON/OFF를 비침습 동등 모드라고 볼 수 없다. 그러나 진단OFF에서도 높은 PER이 있었고 RXFSL은 핸들러보다 먼저 발생했으므로 이것을 전체 N3 고손실의 확정 원인으로 삼지 않는다.

## 4. 기존 실험이 배제하지 못한 것

- fast/full N3합산35.133/31.8%: fast만 제거하면 해결되지는 않았다. 작은 추가 악화 가능성은 미배제.
- G400 SPI ON/OFF/ON N3 40.7/45.7/41.0%: 현재 SPI OFF는 옛 rev24가 아니다. 공통 RX 로직 회귀는 미검증.
- N3를 같은 절대 시각2297us에서 첫 슬롯으로 수신12.4%, 두 번째 슬롯 수신49.2~52.2%: 절대 송신 시각만으로 설명되지 않는다. 비교 당시 보드는888이었다.
- 오늘4212 원위치37.2~39.1% → 이동0% → 복귀27.3%: 전파 경로 영향의 강한 단서이나 금속 단일 원인 또는 펌웨어 무관의 증명은 아니다.
- 시스템 카운터0은 잘못된 버퍼 처리 등이 관측되지 않았다는 뜻이지 수신 감도가 과거와 같다는 뜻이 아니다.

## 5. 다음 권장 검증 — 미실행

1. 원위치·보드·역할·전원·방향을 고정한다. N3는 일단4212 그대로 유지한다.
2. 정확한 과거 네 바이너리와 현재 소스를 M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF로 맞춘 네 바이너리를 비교한다. 현재에는 fast/IRQ/진단/기타profile OFF, SPIopt ON을 사용하고 차이를 명시한다. 비교 전 바이너리·PHY·스케줄 호환성을 검증한다.
3. old→current→old 순으로 시작하고 반복/교차한다. 고정 순서의 시간 혼입도 고려한다. 기존 펌웨어와 로그를 보존하며 예전 소스를 추정해 덮어쓰지 않는다.
4. 과거가 일관되게 낮고 현재가 높으면 펌웨어 회귀 근거다. 이후 oldTX/currentINIT 및 currentTX/oldINIT 비교로 RX/TX를 분리하고, 확인된 쪽의 변경을 좁힌다.
5. 둘 다 높으면 현재 경로/환경의 영향이 크다는 근거지만 소프트웨어 영향 전체가 배제되는 것은 아니다. 상대 차이와 반복을 보고, 필요시888 비교를 별도 요인으로 추가한다.
6. 둘 다 낮으면 G200/SB2000/SP2002 복원 요인을 하나씩 분리한다. G250이나 긴 wait에 의한 회복을 코드 효과로 잘못 부르지 않는다.

분석 이후의 코드 수정 후보는 오류 이벤트 시각/상태를 일관되게 보존하고 추정 라벨을 실시간 슬롯 제어와 분리하는 것이다. 다만 이 변경을 과거 바이너리 대조 전에 섞으면 비교 기준이 다시 바뀐다. 현재 턴에서는 구현하지 않았다.
