# BRRS 실험 3 빠른 실행 가이드

상세 원리, 이론값, 로그 저장법은 `EXP3_EXTTXE_README.md`를 기준으로 한다.
과거 RX 상태 폴링 기반 가이드는 더 이상 사용하지 않는다.

## 고정 조건

- DATA preamble: 32 symbol
- DATA PSDU: 26 B = 8 B protocol header + 16 B application + 2 B FCS
- DATA rate: 6.81 Mb/s
- PAC8, STS off
- 1,000 EXTTXE capture/run
- INIT와 NORMAL은 같은 A/B/C 프로파일 사용

SFD 종류와 PHR rate는 비컨으로 전달하지 않는다. 이상적 BRRS에서는 제거할
필드이고, 실험 3에서는 제거 대상 airtime을 분리하기 위한 사전 합의 빌드
프로파일로만 사용한다.

## SES Build and Debug

| 조건 | TX/NORMAL | RX/INIT | 목적 |
|---|---|---|---|
| A | `Exp3_A_Normal` | `Exp3_A_Init` | SFD8 + STD PHR 기준 |
| B | `Exp3_B_Normal` | `Exp3_B_Init` | `B-A`: SFD 8 symbol 추가 시간 |
| C | `Exp3_C_Normal` | `Exp3_C_Init` | `A-C`: STD/DTA PHR 차이 |

각 조건에서 NORMAL을 먼저 실행하고 INIT를 실행한다. RTT channel 1 로그를
TX와 RX에서 각각 저장한다.

## 완료 조건

TX 로그:

```text
EXP3_TX_RESULT,...attempts=1000,success=1000,captures=1000,status=PASS
```

RX 로그:

```text
EXP3_RX_DONE,...expected=1000,...,status=PASS
```

논문 주 측정값은 TX의 EXTTXE 폭이다. RX 결과는 A/B/C 링크가 정상적으로
동작했음을 확인하는 보조 지표로 사용한다.
