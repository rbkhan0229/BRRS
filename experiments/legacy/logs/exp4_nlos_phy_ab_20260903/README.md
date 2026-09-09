# NLOS 6.9 m M32 PHY 전환 A/B — 2026-09-03

## 결과 — ① PHY 전환 비교 6회 완료

**세 비교 쌍에서 모두 full configure(OFF)의 N3 PER가 더 낮았지만, OFF에서도 31–32.2%의 큰 손실이 남았다.** fast switch가 전혀 무관하다고 할 수 없고, 반대로 fast switch만 제거하면 회귀가 해결된다는 결과도 아니다. 최적화의 PER 비열화를 입증하지 못했으므로 다음 원인분리 단계에서는 보수적으로 full configure를 사용한다. 공식 동결 설정을 바꾼 것은 아니다.

| 순서 | PHY | run | 종료 시각 KST | RX / 예정 보고 수 | 전체 PER | N2 PER | N3 PER | N4 PER |
|---|---|---|---|---|---|---|---|---|
| 1 | fast ON | 1 | 22:19:20 | 2637 / 3000 | 12.100% | 0.1% | 34.5% | 1.7% |
| 2 | full OFF | 1 | 22:21:18 | 2673 / 3000 | 10.900% | 0.0% | 32.2% | 0.5% |
| 3 | fast ON | 4 | 22:22:36 | 2622 / 3000 | 12.600% | 0.0% | 36.4% | 1.4% |
| 4 | full OFF | 4 | 22:24:10 | 2668 / 3000 | 11.067% | 0.0% | 32.2% | 1.0% |
| 5 | fast ON | 7 | 22:25:46 | 2640 / 3000 | 12.000% | 0.1% | 34.5% | 1.4% |
| 6 | full OFF | 7 | 22:27:24 | 2674 / 3000 | 10.867% | 0.0% | 31.0% | 1.6% |

| 합산 | fast ON | full OFF |
|---|---|---|
| 전체 유효 RX / 예정 보고 수 | 7899 / 9000 | 8015 / 9000 |
| 전체 PER | 12.233% | 10.944% |
| 전체 PER Wilson 95% | 11.572–12.926% | 10.316–11.606% |
| N3 RX / 예정 보고 수 | 1946 / 3000 | 2046 / 3000 |
| N3 PER | 35.133% | 31.800% |
| N3 PER Wilson 95% | 33.445–36.860% | 30.158–33.489% |
| 전체 PER 반복 간 표본 SD (%p) | 0.321 | 0.107 |
| 기존 5% PER 성능 판정 | 3/3 FAIL | 3/3 FAIL |

N3의 쌍별 ON−OFF 차이는 +2.3, +4.2, +3.5%p이며 합산 차이는 +3.333%p다. 이 단일 보드/위치, 각 모드 3회, 고정 ON→OFF 순서의 결과를 확정적인 RF 원인이나 모든 보드의 성능 차이로 일반화하지 않는다. 기존 고손실 현상의 큰 부분은 full configure에서도 재현됐다.

Wilson 구간은 패킷 단위 이항 표본을 가정한 기술 통계이며, 시간/슈퍼프레임 내 상관과 고정 실행 순서의 영향을 반영한 인과 검정이 아니다.

### 무결성 및 해석 제한

- 24개 INIT/TX 원시 로그와 metadata, 고정 보드 역할, mode별 firmware hash를 모두 검증했다. [진단 바이너리 manifest](firmware_manifest.json), [읽기 전용 감사 스크립트](audit_phy_ab.py), 실행별 `*.audit.json`에 상세 기록이 있다.
- 모든 실행: deadline miss, delayed RX/TX late, RDB mismatch/incomplete, overrun, SPI 오류/timeout, INIT RX timeout, wrong-source/slot/superframe 및 configuration 오류 = 0. 모든 수집이 종료 마커까지 완료됐다.
- OFF r1 N4만 비컨 999/1000, DATA 999/999 송신이었다. 나머지 모든 TX는 비컨/송신 1000/1000. 이 누락을 삭제하거나 분모에서 제외하지 않았다.
- TX 준비는 모두 첫 시도에서 READY. 캡처 세션에서 USB 재접속/하네스 예외 없음. 사람 통행에 대한 사용자 보고는 이 6회 실행 중 접수되지 않았으며, 실제 통행 부재를 독립적으로 측정한 것은 아니다.
- 무선 RX error 이벤트: ON 298/304/298, OFF 235/262/261. 대부분 `fint-only` (ON 합계 871/900, OFF 741/758)여서 구체적인 CRC/PHR/SFD 실패 단계는 현재 로그만으로 확정 불가. 오류 subtype 합계는 동시 상태 비트 때문에 일반적으로 배타적인 분할이 아니다.
- 모든 실행은 PER 초과로 FAIL을 유지했다. 감사는 그 FAIL을 기록한 채 조기 종료 이후의 시스템 검사도 수행하며, 기준을 완화해 PASS로 바꾸지 않는다.
- first-RX arm 최악 최대: ON 1368 us, OFF 1596 us. RX-open 최소 여유: ON 574 us, OFF 346 us. next-SYNC 준비 최악 최대: ON 1389 us, OFF 1638 us. 각각 최소 잔여 lead 609/361 us. 따라서 이 비교에서 측정된 arm deadline 부족은 보이지 않는다. 이것이 아날로그 안정화/감도 동등성을 보장하지는 않는다.
- 두 모드 모두 RX hot-path 최대 202 us. ON p99 202 us, OFF p99 201 us. mode flag는 부팅 자기검증과 전체 전환 경로를 포함하므로 성능 차이를 특정 생략 레지스터 하나에 바로 귀속할 수 없다.
- 코드를 읽어 확인한 후보 차이는 full path가 반복 적용하는 SYS_CFG/STS/PDOA, DTUNE3, DGC/RX tuning, TX_CTRL 및 조건부 온도/VDDDIG 처리다. fast path의 부팅 자기검증은 이들 전체 상태나 RF 감도를 검증하는 시험이 아니다. 아직 어느 차이가 손실에 기여했는지 입증하지 않았고 소스 수정도 하지 않았다.

### 다음 단계

② SB 비교는 **full configure OFF, optimized SPI ON, SP=2002 고정**에서 SB=2000 vs 1703을 각 3회 교차 실행한다. 이 PHY 선택은 후속 원인분리용이며 최종 채택이 아니다. 시스템 late/error가 발생하면 더 짧은 조건을 성공으로 처리하지 않는다. SPI/과거 바이너리 비교와 최종 A/B/C/6-TX 검증은 아직 남아 있다.

## 목적

현재 유전원 허브 구성에서 전체 `dwt_configure()`와 retained-PGF fast switch를 비교한다. 사용자가 유지한 동일 위치·방향, 동일 보드 역할에서 PHY 전환 옵션만 바꿔 M32 고손실 회귀의 원인을 좁힌다. 차량 성능은 이 실험으로 추정 확정하지 않는다.

## 사전 확정 조건 및 순서

- M32/PAC8/S3/G200, lead 15 us, 1000 superframes/run.
- **두 모드 모두 SB/SP=2000/2000 us**, optimized polling SPI ON.
- IRQ, PHY/RX-path/SPIM profiling OFF. PGF skip OFF.
- INIT(local) 1050270933; N2(remote) 1050211584; N3(remote) **1050273888**; N4(remote) 1050282818.
- 기존 허브를 어댑터 연결 가능한 허브로 교체하고 외부 전원 연결한 상태를 고정. 보드 배치는 사용자 확인. 허브/어댑터 모델·정격, 포트/케이블 라벨, 실제 보드 전압은 아직 별도로 식별/측정하지 않았다.
- 실행 순서: **fast ON r1 → full OFF r1 → fast ON r4 → full OFF r4 → fast ON r7 → full OFF r7**. 모드별 3회, 총 6회. Run ID 1/4/7은 역할 rotation=0을 위한 번호다.
- 순서는 사전 고정이며 무작위화하지 않았다. 시간 순서와 반복별 변동을 함께 제시한다.
- 사용자가 사람 통행을 알리면 해당 실행을 표시·보존하고 비교 포함 여부를 명시한다. 통행 보고가 없다는 것이 무인 환경의 독립적 확인을 의미하지 않는다.
- 5% PER 기준을 유지하며 성능 실패와 시스템 무결성 실패를 구분한다. 0-valid-RX, 수집 불완전, 시스템 오류 실행은 PASS로 인정하지 않는다.

## 동결 보존 및 빌드

- 기준 commit: `f420f95e29351e4b737cc76561bffae7191f4376` (firmware source freeze `479e1ea4b41274428778eb70c11d468d95b9d7a3`).
- 로컬/원격 진단 branch: `exp4-nlos-phy-ab-20260903`, 같은 commit에서 생성.
- 로컬/원격 진단 worktree: `/Users/songchieon/Desktop/DWM3000/DW3_QM33_SDK_1.0.2_exp4_phy_ab_20260903`.
- 기존 `exp4-final-eval-20260902` 작업트리, 바이너리, 과거 실험 결과는 보존. 기존 SB2000/SP2000 이미지도 덮어쓰지 않고 진단 작업트리에서 재빌드한다.
- 펌웨어 소스 변경 없음. 기존 build flag `BRRS_OPT_PHY_FAST_SWITCH`만 모드 간 다름. 두 모드 모두 네 역할 전체를 동일 소스/컴파일러/SDK로 빌드한다.
- SEGGER Embedded Studio/emBuild 8.28; nRF5 SDK v17.0.2 (`sdk/documentation/release_notes.txt` 확인). SPI 32 MHz, CPU 64 MHz; Debug O0, 기존 직접 SPI hot 함수 O3.
- INIT ELF `.comment` 확인: GCC 15.2.1 20251203 (arm-15.2.Rel1), SEGGER C/C++ runtime/compiler signature 20.1.3 (clang 20.1.8 기반). Linker GNU ld 2.45.1.20251203. 모드 간 같은 도구 사용.
- SDK는 git 관리 밖 설치 파일이므로 진단 작업트리에 기존 SDK 디렉터리를 가리키는 링크를 추가했다. 최초 SDK 미연결 빌드 실패 로그는 `exp4_32_s3_init.missing_sdk.build.log`로 보존한다. 보드에 실패 이미지를 플래시하지 않았다.
- 이 SDK 링크는 로컬 진단 작업트리에서 의도된 untracked 항목이다. 추적 소스 diff는 없으며 원래 동결 작업트리는 clean이다. 수집 metadata의 clean 표시는 기존 스크립트 정의에 따라 추적 파일 기준이다.
- 로컬에서 빌드한 동일 HEX/ELF를 원격으로 전달하고 해시를 대조한 뒤 `--no-build`로 수집한다. 비교는 INIT와 TX 3대 모두 같은 모드를 사용하는 시스템 수준 비교이며, 차이가 있어도 RX/개별 TX 어느 쪽 원인인지 바로 확정하지 않는다.

Build commands (API directory):

```sh
./brrs_exp4_build.sh 32 3 200 all 15 --pac 8 --sync-buffer 2000 --sync-prep 2000 --cycles 1000 --spi-opt --phy-fast-switch
./brrs_exp4_build.sh 32 3 200 all 15 --pac 8 --sync-buffer 2000 --sync-prep 2000 --cycles 1000 --spi-opt
```

## 결과 위치와 판정

- fast ON: `../exp4_nlos_phyab_6.9m_g200_l15_pac8_sb2000_sp2000_20260903_spiopt_phyfast/`
- full OFF: `../exp4_nlos_phyab_6.9m_g200_l15_pac8_sb2000_sp2000_20260903_spiopt/`
- 총/노드별 PER, 예정 보고 수와 실제 TX/RX, 비컨 누락, 반복별 Wilson 구간, RX 오류 subtype, 시스템 오류, timing margin을 분석한다.
- SYS_STATUS 오류 subtype은 다음 RX 재가동 후 읽으므로 subtype이 사라진 경우 `fint-only`로 남는다. 이 경우 실제 PHR/CRC/SFD 오류로 임의 분류하지 않는다. 여러 상태 비트가 동시에 설 수 있어 subtype 합계를 무조건 서로 배타적인 패킷 수로 취급하지 않는다.
- 후속 SB 비교는 **SP=2002 고정**으로 별도 수행한다. 이번 2000/2000 결과를 기존 1703/2002와 비교해 SB만의 효과라고 주장하지 않는다.

현재 상태: ① 무선 A/B 6회 완료 및 원시 로그/해시 검증 완료. ②용 별도 파라미터 이미지 빌드를 시작했다. 기존 동결 작업트리와 추적 소스는 변경하지 않았고 push하지 않았다.
