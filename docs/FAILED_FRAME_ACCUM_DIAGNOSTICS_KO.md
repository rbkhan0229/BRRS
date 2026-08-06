# 실패 프레임 accumCount 진단

## 목적

Stage 0과 실험 1~3에서 수신 실패 직후의 DWM3000 Ipatov
`accumCount`를 읽어 실패 단계와 누산량의 관계를 확인한다. 개별 프레임을
RTT로 출력하지 않고 RAM에서 원인별 히스토그램을 만든 뒤 최종 통계에만
출력하므로 반복 중 로그 부하를 늘리지 않는다.

실험 4는 짧은 슬롯 간 재예약 시간을 보존하기 위해 이 진단을 사용하지 않는다.

## 유효성 기준

실패 이벤트의 `SYS_STATUS`에 `RXPRD`(preamble detected)가 있고,
`1 <= accumCount <= PLEN`일 때만 `valid`로 분류한다.

- `SFDTO`, `PHE`, `FCE`, `FSL`: `RXPRD`가 함께 있으면 현재 프레임의
  부분/완료 누산값으로 분석할 수 있다.
- `FWTO`, `PTO`: `RXPRD`가 없으면 CIA 진단 레지스터가 갱신되지 않았을 수
  있다. 읽힌 값은 `invalid_raw`로 보존하지만 이전 프레임의 잔존값일 수
  있으므로 accumCount 측정값으로 사용하면 안 된다.

## 최종 로그

```text
FAIL_ACCUM_SUMMARY_CSV,N2,sfdto,events=...,diag_ok=...,valid=...,invalid_no_rxprd=...,invalid_zero=...,invalid_range=...,read_fail=...,hist_overflow=...
FAIL_ACCUM_HIST_CSV,N2,sfdto,valid,accum=8,n=...
FAIL_ACCUM_HIST_CSV,N2,fwto,invalid_raw,accum=0,n=...
```

`hist_overflow`가 0이 아니면 한 원인에서 서로 다른 raw 값이 16종을 넘은
것이다. 이 경우 히스토그램 용량을 늘린 뒤 해당 조건을 다시 측정한다.

## 해석 원칙

1. `valid` 히스토그램만 실패 프레임의 accumCount 분포로 사용한다.
2. `invalid_raw`는 레지스터가 읽혔다는 사실만 보여 주며 물리적 누산량을
   의미하지 않는다.
3. 성공 프레임 히스토그램과 실패 프레임 히스토그램을 같은 lead 조건에서
   비교한다.
4. FWTO가 대부분이면 현재 하드웨어 진단만으로는 실패 accumCount를 얻을 수
   없다는 결과 자체를 기록한다. 프리앰블 검출 이전의 내부 상관 진행량은
   DWM3000 공개 API의 accumCount로 관측할 수 없다.
