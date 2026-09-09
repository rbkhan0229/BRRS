# 양쪽 배포·보드 제어 보완과 실제 Stage0 1:1 확인

2026-09-07, KST. **유효 실행에서 N4→RX 2,000/2,000 수신, PER 0%.** 공통 설정에서 이미지 준비→양쪽 배포→TX READY→RX 시작→기존 캡처/검증→metadata 대조→flash readback을 실제 장비로 확인했다. 전체 Stage0 탐색 또는 TX6 Exp4 검증 완료는 아니다.

‘RF를 하지 않았다’는 말은 직전의 전체 단계 빌드·설정·모의 수집 점검에 새 무선 측정이 포함되지 않았다는 뜻이었다. 이전 NLOS/집 Exp4 PER 측정은 실제 RF 실험이 맞다. 이번에는 실제 무선 실행 2회 중 보드 제어 결함이 있었던 첫 실행을 제외하고, 수정 후 1회만 유효 결과로 남겼다. 첫 실행을 정상 반복으로 합산하지 않는다.

## 연결과 실제 조건

- SSH `s-macbook-air` 정상. 16:57 사전 및 17:09 종료 점검에서 로컬 INIT1·원격 TX6 모두 인식. N6 `1050227627` 포함. 남은 RTT/실험 프로세스 없음.
- RX `1050270933`, 물리 TX **N4 `1050282818`**. 단일 링크 TX의 논리 ID는 **N2**이므로 원시 로그의 N2를 실제 N2 보드로 오해하지 않는다.
- 비참여 N2 `1050211584`, N3 `1050273888`, N5 `1050208509`, N6 `1050227627`, N7 `1050204212`는 기존 flash를 유지하고 MCU 정지로 관리했다. 최종 실행의 준비 전후 정지 상태를 확인했다.
- 현재 설치된 준비 환경 그대로 사용. 위치·방향·케이블·허브 포트·전원을 변경하지 않았다. TX 허브 외부 전원은 사용자 확인에 따라 연결 상태로 기록. USB 트리는 pre/postflight JSON에 보존. 현재 차량 모델·거리·실제 채널은 확인되지 않아 `vehicle_preparation`, distance=null로 기록했다.
- **Stage0 M32/PAC8, lead25us, tail0, delayed RX, 2,000 superframes**. SF10,000us, 첫 DATA RMARKER=beacon+3,000us, 수신창122us, 로그상 RX-open→RMARKER67us. PHY는 기존 Stage0 기본값이며 DATA SFD timeout33이다.
- Stage0의 SLOT597us(airtime97+guard500) 기본값을 사용했다. **Exp4 G250/13슬롯 조건을 재현한 실험이 아니다.** lead25도 PAC별 최적값으로 동결하지 않았다.

| 대상 | 준비 이미지 | SHA256 |
|---|---|---|
| RX 1050270933 | Stage0_L25_T0_Init, M32/PAC8/lead25 | `4153b2b9143342b125a78bd09cded3e9de484a91e648a168c2fdb0e5a4e2c791` |
| 물리 N4 1050282818 / 논리 N2 | Stage0_Normal | `c8bda52ee6de8bd0fe3caea8100a61803ac9682a8314ac7d9a4351545acb6328` |

RX 이미지 1개를 새로 빌드하고, 이미 검증된 TX 이미지는 재사용했다. 두 실행의 HEX/ELF hash는 동일하다. 첫 실행 이후 변경한 것은 보드 제어 도구뿐이다. 각 실행에서 RX134,117bytes, TX129,613bytes의 HEX에 들어 있는 전체 flash 데이터를 읽어 대조했다.

## 실행 결과

| 실행 / KST(설정·수집·검증 포함) | N4 offered | TX attempt/success | RX | PER | 사용 여부 |
|---|---:|---:|---:|---:|---|
| 최초, 17:03:18~17:04:09 | 2,000 | 2,000 / 2,000 | 2,000 | 0% | 제외: 비참여 TX 정지 유지 검증 실패 |
| 제어 수정 후, 17:07:20~17:08:02 | 2,000 | 2,000 / 2,000 | 2,000 | **0%** | **유효, 해당 N4 단일 링크 PER<1% 통과** |

유효 실행은 비컨 대기 중인 N4의 READY를 17:07:32에 확인한 뒤 RX를 시작했다. RX/TX 모두 READY와 각 종료 마커가 정확히 1개, RTT 및 단계 검증 정상, timeout 없이 종료했다. RX의 END 송신3개와 TX의 END 수신1개도 확인했다.

| 유효 실행 카운터 | 값 |
|---|---:|
| SFD timeout / PHR error / CRC error / RXFSL | 0 / 0 / 0 / 0 |
| RX frame wait timeout(FWTO) / preamble timeout(PTO) | 0 / 0 |
| delayed-RX late / delayed-TX late | 0 / 0 |
| RX DATA config error | 0 |
| TX SYNC loss timeout / SYNC RX error | 0 / 0 |
| TX beacon config error / DATA config error | 0 / 0 |
| TX attempt / success | 2,000 / 2,000 |
| 실제 수집 timeout | 없음 |

별도 비컨 수신 총수 카운터는 이 Stage0 요약에 없다. 비컨에 따라 2,000회 송신했고 SYNC loss=0임을 확인했다. wrong source/slot/superframe의 개별 카운터도 이 Stage0 요약에 별도 출력되지 않으므로 0으로 채우지 않았다. Exp4의 RDB mismatch/incomplete/resync/overrun 검증은 이 단일 버퍼 Stage0 경로에 적용한 실험이 아니다.

## 발견해 수정한 실행 도구 문제

첫 실행은 무선 및 수집이 정상이어도 종료 후 N2의 MCU가 정지 상태가 아니어서 전체 통과를 거부했다. J-Link는 기본적으로 연결을 닫을 때 CPU 실행을 재개한다. 기존의 `halt→close`만으로는 비참여 TX 정지를 유지할 수 없었다. [SEGGER SetRestartOnClose 문서](https://kb.segger.com/J-Link_command_strings#SetRestartOnClose)

새 `brrs_suite_case.py`의 보드 제어 연결에 `SetRestartOnClose=0`을 적용했다. 모든 지정 보드를 정지한 다음 연결을 닫고 다시 열어도 정지 상태인지 확인해야 TX 캡처를 시작한다. 종료 후에도 비참여 TX5의 정지를 확인한다. 오류 시 해당 실행의 캡처 프로세스만 정리하고 보드 정지를 시도하며, 원격에는 명시적 STOP 파일과 제한 시간을 사용한다.

따라서 두 번째 실행은 불필요한 동일 조건 반복이 아니라 **검출된 제어 결함 수정에 필요한 검증**이었다. 이것이 과거 Exp4 PER 고손실의 원인이었다고 주장하지 않는다.

## 배포·검증 범위

공통 manifest에서 선택한 case마다 기존 단계 캡처 CLI의 `--build-only`로 HEX/ELF/RTT 주소를 확인하고, 전체 단계 도구와 해당 case 이미지들을 독립 실행 폴더에 묶는다. 양쪽 runtime은 `--no-build`만 사용한다. 이전 Git 작업 트리를 원격에서 덮어쓰거나 양쪽에서 따로 빌드하지 않는다. 전체 조건 이미지가 모두 만들어졌다는 뜻은 아니다.

- 수정 후 bundle: `stage0_m32_pac8_l25_controlfix/`; 파일76개 배포. 양쪽 payload index SHA256 일치: `3d7cc949f34ddae3b225e1de6cc64035bb1bd8565e970e8b9a09284d6312709b`.
- exact probe set, 고정 serial, 이미지/도구 hash, case/조건 hash, 물리 역할/논리 ID metadata를 확인.
- 잘못된 원격 배포 hash 및 이미지 변조는 보드 접속 전 거부하는 검사 통과. close 시 자동 재시작을 모사한 회귀 검사 통과. 0수신은 INVALID, 정확히1%는 FAIL_PER로 판정하는 검사 통과.
- 정상 실행에서 TX READY→RX 시작, 기존 두 캡처/검증, flash readback, TX5 정지 유지, 실제 raw 로그의 합산 판정까지 확인.
- Stage0 이외 단계는 앞선 빌드/설정 점검과 공통 실행기 경로가 준비된 상태다. 새 실행기로 각 단계를 모두 실제 RF 검증했다고 표현하지 않는다. 각 단계의 최종 조건 이미지는 PAC별 lead 선정 후 준비한다.

원본 Git 4개(로컬/원격 main·analysis)의 branch/HEAD/dirty/diff hash는 사전과 같다. 로컬 main `exp4-final-eval-20260902@f420f95`, 로컬 analysis `exp4-rx-error-diag-20260904@55a23fa`와 기존 sdk 항목, 원격 main `main@999df79`의 기존12항목 및 원격 analysis clean을 보존했다. commit/push 없음.

## 현재 보드 상태와 다음 단계

RX와 물리 N4에는 위 Stage0 RX/TX 이미지가 남아 있고 실험 END 상태다. 나머지 TX5는 이전 Exp4 flash를 유지한 정지 상태다. Exp4를 다시 실행할 때는 N4를 원래 논리 N4 이미지로 포함해 정확한 case 이미지를 배포해야 한다. 케이블을 다시 꽂거나 전원을 바꾸면 MCU 정지가 유지된다고 가정하지 않고 다음 case 준비에서 재확인한다.

유효 결과는 **Stage0 PAC8/lead25 한 점**이다. 남은 PAC4/PAC8 lead 탐색에서 이 점을 같은 환경·절차라면 재사용하고, 모든 조건을 불필요하게 반복하지 않는다. PAC별 lead는 아직 null/미선정이며, 먼저 Stage0의 서로 다른 lead 조건을 비교해 정해야 한다. 이후 그 설정으로 Exp1/2와 TX6 Exp4를 검증한다. 특히 N4가 단일 첫 슬롯에서0%였다는 결과만으로 다중 슬롯의 약한 링크 문제가 해결됐거나 manual-rearm 가설이 증명됐다고 결론내리지 않는다.

근거: [합산 결과](SUMMARY.json), [유효 실행 판정](stage0_m32_pac8_l25_controlfix/ASSESSMENT.json), [RX 원시 로그](stage0_m32_pac8_l25_controlfix/results/local/init.log), [N4 원시 로그](stage0_m32_pac8_l25_controlfix/results/remote/N4.log), [제외 실행 판정](stage0_m32_pac8_l25/ASSESSMENT.json).
