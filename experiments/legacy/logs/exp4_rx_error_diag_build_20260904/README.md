# Exp4 RX 오류 진단 — 오프라인 빌드 보관

2026-09-04. 실험 장비가 정리된 상태에서 만든 **빌드/단위 테스트 결과**다. 실제 수신 로그, PER 결과, 계측 오버헤드 실측은 포함하지 않는다. 플래시·SSH·무선 실행·push를 하지 않았다.

## 소스와 보존 상태

- 작업 폴더: `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_exp4_rx_error_diag_20260904`
- 브랜치: `exp4-rx-error-diag-20260904`
- 펌웨어 소스 커밋: `4f0c9be67f9bd903d7a55f13ce55673719208cbf`
- verifier/테스트 최종 커밋: `55a23fa94fb7b4e372ec7b03d54be72f01b81e7a`
- 후속 커밋은 verifier·Python 테스트·문서만 변경했다. 펌웨어/빌드 스크립트/프로젝트 설정은 두 커밋 사이 동일하다.
- 기준 동결 브랜치 `exp4-final-eval-20260902`와 이전 진단 브랜치 `exp4-nlos-phy-ab-20260903`는 `f420f95e29351e4b737cc76561bffae7191f4376` 그대로다. 과거 실험 로그를 덮어쓰지 않았다.
- 새 worktree의 유일한 untracked 항목은 로컬 SDK symlink다. SDK를 커밋하지 않았다.

## 이 번들의 설정

M32 / PAC8 / S3 / G200 / lead15µs / SB2000µs / SP2002µs / 1000슈퍼프레임. polling + 최적화 SPI, 전체 `dwt_configure()`(fast switch OFF), 다른 프로파일링과 IRQ OFF. DATA 예산5998µs.

이는 중단 직전 원인 분리 조건을 위한 준비 이미지이며, 최종 동결 설정의 SB1703/SP2002·fast switch ON을 대체한다고 확정한 것이 아니다. 실험 재개 시 필요한 비교 조건을 먼저 확인한다.

`diag_on/`과 `diag_off/`에는 INIT/N2/N3/N4의 HEX·ELF·build.log가 있다. 차이는 INIT의 `BRRS_OPT_RX_ERROR_DIAG=1` 여부다. OFF는 기본값0. ON4개를 최종 펌웨어 소스로 재빌드하고 OFF INIT를 재빌드했다. OFF의 TX는 앞선 동일 설정 빌드에서 재사용했으며, 최종 ON TX와 HEX 및 ELF SHA256이 각각 완전히 동일함을 확인했다. TX build.log는 실제 개별 빌드 로그라 서로 다를 수 있다.

이 번들은 보드에 올리지 않았다. **INIT/N2/N3/N4는 논리 역할이며 실제 보드 일련번호는 미지정**이다. 재설치 후 888/새 보드의 연결과 역할을 확인해 배정해야 한다.

## 빌드 환경 및 검증

- SEGGER Embedded Studio8.28, nRF5 SDK17.0.2.
- Debug 설정의 최적화 `None`. SPI 설정: 초기4MHz, DATA SPIM3 fast32MHz. 무선/guard 정책 변경 없음.
- ELF `.comment`: GCC arm15.2.Rel1 기반15.2.1(20251203), SEGGER C/C++20.1.3(clang20.1.8 기반). 이는 ELF에 포함된 컴파일러 식별 문자열이며 로컬 Homebrew GCC16.1.0과 구분한다.
- Python15개 테스트 PASS (`host_tests.log`). C collector ASan/UBSan PASS (`collector_test.log`). 셸 문법과 `git diff --check` PASS.
- 최종 Exp4 ON4개/OFF INIT 빌드 성공, compiler error/warning 없음.
- Exp1/Exp2 M32 INIT 빌드 성공. 공유 코드의 Exp4 전용 함수 호출 회귀를 수정했다. 이전 실패 로그 `compat/exp1_build.log`도 보존했다. 수정 후 로그는 `exp1_fixed_build.log`, `exp2_build.log`.

| INIT | text | data | BSS |
| --- | ---: | ---: | ---: |
| 진단 OFF | 158205 | 164 | 90540 |
| 진단 ON | 162253 | 164 | 95820 |
| 차이 | +4048 | 0 | +5280 |

모든 보관 파일의 SHA256은 `SHA256SUMS.txt`에 있다. 이 디렉터리에서 `shasum -a 256 -c SHA256SUMS.txt`로 검증한다.

## 다음 실험의 제한

새 기능은 오류/timeout 이벤트 분기의 재시작 전후 SYS_STATUS48비트, FINT, 슬롯 추정 문맥을 RAM에 보관하고 종료 후 출력한다. 전체 이벤트의 비트별 집계와 최초64개 원시 예시를 제공하며, 큐 overflow/로그 누락/카운터 불일치/시스템 경고를 성공으로 판정하지 않는다. 단, 무이벤트 미수신이나 RXOK와 오류가 동시에 발생해 good 분기로 간 경우를 모두 설명하는 진단은 아니다.

장비 재설치 → 현재 연결/역할 확인 → 같은 보드·위치·방향·전원·PHY·wait의 OFF 기준 재확보 → ON/OFF 반복 비교 순으로 진행한다. 오류 경로의 추가 SPI 읽기 비용을 실제 G200에서 확인하기 전에는 원인 판별 능력이나 PER 비회귀를 확정할 수 없다. 888/새 보드 비교도 동일한 진단 설정끼리 해야 한다.
