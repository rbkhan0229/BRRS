# CIR 전력 변환의 품질 문제

분석 범위는 로컬 원문과 저장된 ELF다. firmware·raw·HEX를 수정하거나 보드를 재실행하지 않았다.

## RSSI -128dBm

M32/PAC4:800/1000, 이전 M32/PAC8:1000/1000. `deca_rsl.c`의 계산은 power 또는 accumulation이0이면 SHRT_MIN(-128dBm)을 반환하고, 계산값이 표현 하한 아래여도 같은 값으로 제한한다. 상위 `ull_calculate_rssi()`는 이 출력 자체를 실패로 바꾸지 않고 DWT_SUCCESS를 반환한다. 현재 기록의 accum은 양수지만 원래 diagnostics power/DGC가 없어 나머지 원인을 구분할 수 없다. nonfloor 부분만의 평균도 선택 편향이 있으므로 M별 평균 RSSI 비교의 근거로 쓰지 않는다.

## FP power의 RX code 절단

대상 [driver](/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/Shared/dwt_uwb_driver/dw3000/dw3000_device.c:8227)의 식은 다음과 같다.

```c
uint8_t rx_pcode = (uint8_t)(reg & 0x1F00) >> 8;
```

0x1F00은8~12bit이므로 uint8_t cast가 먼저 모든 비트를 버린다. RX code9라면 원래0x0900을8bit로 자른0을 다시 shift하므로0이 전달된다. 올바른 연산 순서는 mask→shift→8bit cast이다. 이는 후속 수정안이며 현재 firmware에는 적용하지 않았다.

Exp2의8개 RX ELF와 Exp5 RX ELF에서 레지스터 read 뒤 `movs r3, #0`을 확인했다. [ELF별 근거](fp_power_evidence.json)와 각 `*_fp_power.disassembly.txt`에 주소·명령을 저장했다. DATA config의 RX code는9이며 [RSL 상수](/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_vehicle_suite_fix_20260907/Drivers/API/Shared/dwt_uwb_driver/deca_rsl.c:17)는 PRF64=31155/256, PRF16=29133/256dB다. 따라서 잘못된 분기는 FP power를 `(31155−29133)/256 = 7.8984375dB` 높인다.

`FP_alpha_only_estimate = logged_FP_dBm − 7.8984375dB`를 별도 열로 제공했다. 로그의0.01dB 반올림 오차는 남으며 안테나·보드별 절대 전력 교정, 하드웨어 diagnostics 정확성, 포화 여부를 보증하지 않는다. 원래 FP/RSSI-gap을 덮어쓰지 않았다. 기존 M64 이상에서 FP power가 전체 RSSI보다 높았던 약3~4dB 역전은 이 오류와 일관되지만, M32 RSSI 하한값 문제를 이 상수만으로 해결할 수는 없다.

## FP-SNR와 PER에 미치는 범위

FP-SNR는 `max(power[FP−1..FP+3]) / mean(power[FP−14..FP−3])`이고 저장된 정수 peak/noise/ratio를 모두 대조했다. 이 경로는 dBm/PRF 상수 계산을 사용하지 않는다. PER는 TX offered/RX frame 집계다. 따라서 위 dBm 버그 때문에 성공 패킷이나 PER를 재분류하지 않았다. Exp4의 continuous RX 원인설을 검증하는 자료도 아니다.
