# Exp4 RX 오류 진단 보완 (오프라인 준비)

진단 브랜치: `exp4-rx-error-diag-20260904`. 기준 `f420f95e29351e4b737cc76561bffae7191f4376`. 동결/기존 실험 브랜치, 과거 로그는 변경하지 않는다. 사용자는 장비를 정리하고 귀가한 상태이므로 이 변경의 검증은 **소프트웨어 테스트와 빌드까지**다. 재현·PER·실측 계측 오버헤드는 검증 전이며, 기존 고손실의 물리 원인을 확정한 수정이 아니다.

## 무엇이 부족했나

Exp1·2는 실제 SYS_STATUS low32를 폴링하고 복구/clear 이전에 `capture_failed_accum()`을 호출했다. FWTO/PTO/SFDTO/PHE/FCE/FSL/OTHER와 accumCount 분포, RXPRD/범위 유효성은 기록했지만 전체48비트 원시 상태나 여러 비트의 동시 발생, 재시작 전후 쌍을 보존한 것은 아니다. CIAERR/ARFE/CPERR는 별도 legacy 카운터가 없었다.

Exp4 최적화 경로는 FINT로 이벤트를 검출한 뒤 재시작하고 SYS_STATUS low32를 읽는다. 실패 accumCount 수집도 제외됐다. `fint-only`는 기존 네 종류(RXSTO/RXPHE/RXFCE/RXFSL)가 보이지 않은 경우를 묶은 카운터이므로 **CMD_RX가 상태를 지웠다는 증거가 아니다**. CIAERR/ARFE 등 기존 분류에서 빠진 오류일 수도 있다. 기존 banner의 잘못된 `fint_fallback_if_cleared` 설명을 `legacy_four_bit_post_read`로 고치고 Exp4 revision48로 표시했다. OFF도 이 설명 수정은 포함하므로 이전rev47 INIT HEX와 동일하다고 주장하지 않는다.

DW3000 User Manual v1.1의 pp93-99(SYS_STATUS), p234(FINT_STAT), p239(CMD_RX)를 로컬 문서와 SDK 비트 정의로 대조했다. SYS_STATUS는 여섯 옥텟이며 일반적으로 latched/W1C다. FINT.RXTSERR는 CIAERR, RXERR는 RXFCE/RXFSL/RXPHE/ARFE/RXSTO/RXOVRR를 포함한다. CMD_RX 설명만으로 오류 비트 소거를 단정할 근거는 없다.

## 구현

- `BRRS_OPT_RX_ERROR_DIAG=0` 기본값, CLI `--rx-error-diag`로 INIT에서만 활성화. IRQ/다른 프로파일링과 혼합은 거부한다.
- 오류/타임아웃 처리 분기에서 CMD_RX 전에 SYS_STATUS 6바이트를 RAM에 복사한다.
- 기존 CMD_RX와 슬롯 추정 이후 SYS_STATUS 6바이트와 FINT를 다시 복사한다. 기존 복구와 legacy 카운터에는 원래처럼 post low32를 사용한다. 진단 추가와 복구 정책 변경을 섞지 않는다.
- 각 기록은 superframe, 처리 전 logical slot, 기존 방식의 estimated slot, host buffer, 감지 FINT/post FINT, pre/post48비트, 실제 rearm 여부를 포함한다. 슬롯 값은 추정 문맥이며 실패 패킷에서 복호한 송신자 ID가 아니다.
- DATA 버스트 내부에는32개 고정 RAM 큐에 복사만 하고, RX 종료/버스트 SPI 종료 후 집계한다. 모든 처리 이벤트를48개 비트별 pre/post 카운터에 반영한다. 원시 예시는 최초64개만 보관하며 이후 예시 생략 수를 명시한다.
- 큐 overflow는 정보 손실이므로 FAIL. 최초64개 이후의 의도된 예시 생략과 구분한다. 원시 로그가 잘리거나 카운터/샘플이 불일치해도 verifier가 FAIL 처리한다.
- pre-status read, post-status read, post-FINT read, event polling 시작부터 rearm 함수 복귀까지의 count/min/max/평균을 출력한다. 마지막 항목은 순수 하드웨어 RX startup 측정이 아니다.
- VWARN/RXOVRR/PLL_HILO/HPDWARN 및 CMD_ERR/SPI_OVF/SPI_UNF/SPIERR 관측 시 PASS를 금지한다. latched bit 관측 횟수이며 독립 물리 고장 횟수가 아니다. SPI CRC 비활성 상태에서 켜질 수 있는 SPICRCE(bit2)는 임의로 고장 판정하지 않는다.
- 새 진단 출력은 실험 종료 때만 수행한다. 기존 PER, TX/유효 RX, deadline, buffer, SPI 검증은 유지하고 새 진단 무결성 검사를 PER 판정보다 먼저 한다.

추가 RAM: collector4760바이트와 최종 출력용512바이트 버퍼 및 정렬(초기 빌드의 BSS 증가5280바이트). 정상 RX-good 서비스에 새 SPI 읽기는 넣지 않았다. **오류 경로에는 읽기와 RAM 기록 비용이 추가되므로 무간섭 계측은 아니다.** 기존 event-to-rearm/deadline 및 next-SYNC margin 검사가 이 비용을 포함한다. 출력은 종료 후 blocking RTT를 사용해 실시간 DATA 처리와 분리한다.

### 출력 레코드

| Prefix | 내용 |
| --- | --- |
| `EXP4_RX_ERROR_DIAG_CONFIG_CSV` | 진단 ON/버전/크기/대상 범위 |
| `EXP4_RX_ERROR_DIAG_CSV` | 이벤트·처리·overflow·예시 생략·경고 요약 |
| `EXP4_RX_ERROR_BIT_CSV` | 0..47 각 비트 이름과 pre/post 발생 횟수 |
| `EXP4_RX_ERROR_SAMPLE_CSV` | 최초64개의 원시 pre/post 상태와 슬롯 문맥 |
| `EXP4_RX_ERROR_TIMING_CSV` | 네 구간 시간 분포 요약 |

## 제한 및 해석 원칙

1. pre/post가 달라도 두 읽기 사이에서 무슨 변화가 있었음을 보일 뿐, CMD_RX 하나의 인과라고 단정하지 않는다. 읽기 자체도 원자적인 RF 상태 관측이 아니다.
2. 비트 카운터는 중복될 수 있다. 합계를 패킷 손실 수와 같다고 해석하지 않는다. progress 비트도 이전 프레임의 잔류 상태일 수 있다.
3. RXOK와 RXERR가 동시에 올라 기존 good 분기로 처리되는 경우, 그리고 아무 오류 이벤트 없이 끝난 미수신은 이 error/timeout-only snapshot 범위 밖이다. 이에 대한 완전한 패킷별 원인 설명을 약속하지 않는다.
4. FWTO와 DATA RX 스케줄/절전 구조를 바꾸지 않았다. 현재 Exp4는 첫 delayed-RX 이후 bounded DATA burst 내부 manual immediate rearm이며 후속 슬롯마다 독립 delayed-RX는 아니다.
5. OFF/ON 비교는 같은 보드·역할·위치·전원·PHY·wait에서 해야 한다. 계측 ON에서 시스템 오류가 생기면 그 데이터로 원래 PER 원인을 단정하지 않는다. 더 여유 있는 guard를 쓸 경우 OFF도 같은 guard로 다시 비교한다.
6. 64개 원시 예시는 최초 구간 편향이 있다. 전체 이벤트 비트 카운터와 반복 실행을 함께 사용한다. 과거 `fint-only` 로그에서 이제 오류 종류를 역산할 수 있는 것은 아니다.

## 공유 소스의 Exp1·2 빌드 회귀 수정

기준f420f95에서 공용 DATA 전환 코드가 Exp4 전용 `brrs_exp4_phy_switch()`를 무조건 호출해 Exp1 컴파일이 실패했다. 이번 브랜치에서는 Exp4만 해당 wrapper를 쓰고 다른 실험은 `dwt_configure()` 전체 설정을 쓰도록 조건을 분리했다. Exp1·2의 오류 수집 정책은 변경하지 않았다. 실패 빌드 로그도 별도로 보존한다.

## 빌드와 테스트 (하드웨어 접근 없음)

`Drivers/API`에서 진단/대조 이미지를 별도 경로에 만든다. shared Debug 출력이 있으므로 빌드는 병렬 실행하지 않는다.

```sh
./brrs_exp4_build.sh 32 3 200 all 15 --pac 8 --sync-buffer 2000 --sync-prep 2002 --cycles 1000 --spi-opt --rx-error-diag
./brrs_exp4_build.sh 32 3 200 all 15 --pac 8 --sync-buffer 2000 --sync-prep 2002 --cycles 1000 --spi-opt
```

두 세트의 TX 컴파일 옵션은 동일하고, 진단 차이는 INIT에만 있다. `_spiopt_rxerrdiag`와 `_spiopt` 이미지/로그 경로 및 메타데이터가 분리된다. 실제 사용 시 capture와 multi-TX에도 `--rx-error-diag`를 넘겨야 하며, 진단 기록이 없으면 ON 실행은 FAIL이다. 동일 세트의 TX HEX를 검증 후 양쪽 경로에 재사용할 수도 있다.

저장소 루트에서:

```sh
python3 -B -m unittest discover -s Drivers/API/tests -p 'test_exp4_rx_error_diag.py' -v
cc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined Drivers/API/tests/rx_error_diag_unit.c -o /tmp/brrs_rx_error_diag_unit
/tmp/brrs_rx_error_diag_unit
```

검증은 RAM collector,48비트 분류, 반복 집계, 기록 생략/overflow, 지연 통계, 진단 레코드 누락/오염, 하드웨어 경고, 플래그 전달/경로 분리/로그 덮어쓰기 거부 등을 포함한다. 실험 대체 데이터가 아닌 synthetic fixture를 사용한다. 실측 로그로도 기존 M256 PASS/M32 고손실 FAIL 및 유효RX0 FAIL 정책을 확인한다.

오프라인 확인 결과: Python 테스트13개 PASS, C collector의 AddressSanitizer/UndefinedBehaviorSanitizer 검사 PASS, 셸 문법 검사 PASS. 공유 호출 수정 후 `Exp1_32_Init`와 `Exp2_32_Init` 빌드가 모두 성공했고 오류/경고는 없었다. 실제 RF 수신과 RTT 수집을 실행한 결과는 아니다. 최종 Exp4 바이너리의 커밋·해시·빌드 결과는 저장소 밖 `logs/exp4_rx_error_diag_build_20260904/`에 별도 기록한다.

## 다음 실험 시작 조건

장비 재설치 후 보드 일련번호·위치·방향·전원부터 다시 확인하고 현재 바이너리를 가정하지 않는다. 원래 고손실 구성의 새 OFF 기준 실행을 확보한 다음 같은 보드에서 ON/OFF를 비교한다. 888과 새 보드 비교는 진단 설정까지 동일하게 맞춘 별도 비교쌍으로 수행한다. 기존52.5%와 재설치 후 결과를 통제된 즉시 A/B로 취급하지 않는다.

물리 시험 전에 firmware commit/HEX hash/보드 역할을 다시 확인한다. 현재 시점에서 PER 개선이나 물리 원인 규명은 주장하지 않는다. 실험 장비에 대한 flash/push/원격 실행은 이 문서의 빌드 검증에 포함되지 않는다.
