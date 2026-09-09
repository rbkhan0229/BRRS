# RX 오류 진단 재개 — 사전 점검에서 중단

최신 상태: **4212 복구 완료, OFF/ON 각3회 실험 완료**. 최종 결과는 [RESULTS.md](RESULTS.md), 집계는 [summary.json](summary.json)에 있다. 아래 사전 점검 기록은 복구 이전의 이력을 보존한 것이다.

2026-09-04 KST. 사용자가 어제와 같은 환경으로 재설치했다고 확인하고 실험 시작을 요청했다. 최초 점검 당시 상태: **BLOCKED_BEFORE_FLASH / RF_NOT_STARTED**. 이 시점에는 측정된 PER 데이터가 없었다.

## 확인한 환경

- SSH `100.115.225.85` 정상. 원격 hostname `songchieons-MacBook-Air.local`, 계정 `songchieon`.
- 기존 J-Link/RTT/Exp4 수집 프로세스 없음(조회 명령 자체 제외).
- 로컬 INIT `1050270933`.
- 원격 N2 예정 `1050211584`, N3 예정 새 보드 `1050204212`, N4 예정 `1050282818`. 기존 `1050273888`은 연결되어 있지 않음.
- 물리 배치는 사용자 확인에 근거한다. 원격으로 위치/방향/허브 어댑터를 직접 관찰한 것은 아니다.

## 플래시 전 SWD 점검

PyLink로 J-Link open → 기준 전압 조회 → SWD/NRF52840_XXAA 연결 → CPUID 읽기를 실행했다. flash/erase/recover/reset 명령은 명시적으로 호출하지 않았다. J-Link 장치별 InitTarget 연결 절차는 실행되었다.

| 대상 | 기준 전압 | 1000kHz SWD | CPUID |
| --- | ---: | --- | --- |
| INIT 1050270933 | 3300mV | 연결 성공 | 0x410fc241 |
| N2 1050211584 | 3300mV | 연결 성공 | 0x410fc241 |
| N3 1050204212 | 3300mV | InitTarget -1 | 읽기 불가 |
| N4 1050282818 | 3300mV | 연결 성공 | 0x410fc241 |

새 N3만 100kHz로 낮춰 재검사했으나 다음 오류가 동일하게 발생했다.

```text
PROBE 1050204212 VTREF_mV 3300
Device "NRF52840_XXAA" selected.
InitTarget() start
InitTarget() end - Took 10.1ms
InitTarget() start
InitTarget() end - Took 10.5ms
J-Link script file function InitTarget() returned with error code -1
PREFLIGHT_FAIL_LOW_SPEED JLinkException('Unspecified error.')
```

이는 실험 중 RF 오류나 PER 관측이 아니다. USB probe 인식/VTref만으로 대상 MCU 전원·SWD 연결·디버그 보호 상태·보드 모델 호환성을 확정할 수 없다. 하드웨어 불량으로 단정하지 않는다. 재연결 및 보드 전원/디버그 설정 확인이 필요하다. 전체 삭제가 수반될 수 있는 recover 명령은 사용하지 않았다.

## 준비된 실험과 보존 상태

- 진단 소스 `55a23fa94fb7b4e372ec7b03d54be72f01b81e7a`, 펌웨어 소스 `4f0c9be67f9bd903d7a55f13ce55673719208cbf`.
- `logs/exp4_rx_error_diag_build_20260904/SHA256SUMS.txt`의29개 파일 검증 모두 OK.
- 계획: M32/PAC8/S3/G200/lead15/SB2000/SP2002/1000SF, full dwt_configure, polling+SPI 최적화, 진단 OFF/ON 반복. 역할 고정, IRQ/기타 프로파일링 OFF.
- 새 N3는 serial 정렬상 가장 작아 기존 multi_tx 자동 정렬/회전으로 원하는 매핑을 만들 수 없다. 다음 실행은 capture를 역할별 `--serial`로 직접 지정하고 TX3대 READY 후 INIT를 시작한다.
- 로컬 전송 준비용 `rxdiag.bundle`(추가 진단 커밋만), `images.tgz` 생성. 원격으로 배포하지 않았으며 원격 진단 worktree도 아직 만들지 않았다.
- 네 보드 모두 플래시하지 않았다. INIT 무선 실행을 시작하지 않았다. 과거 로그/동결 브랜치/push 변경 없음.

다음 작업: N3 새 보드의 물리 연결/전원/디버그 설정 확인 후 SWD 읽기 점검 재실행. 통과한 경우에만 명시적 역할별 플래시와 OFF 기준 수집을 시작한다. 이전888의 PER과 오늘 새 보드 결과는 별도 조건으로 유지한다.

## 재연결 후 추가 확인 및 사용자 승인 복구

위 내용은 최초 중단 시점 기록이다. 이후 사용자가 재연결했지만 NRF52840 연결 실패가 재발했다. Generic Cortex-M4 연결 로그에서는 SW-DP 0x2BA01477 / AP1 IDR0x02880000 응답을 확인했다. 이 연결 과정에서 J-Link가 자동 `connect under reset`을 시도했다(명시적인 reset 호출과 구분). Flash/erase는 그 단계에서는 수행하지 않았다.

CTRL-AP를 직접 확인하니 APPROTECTSTATUS가 세 번 모두0이었다. 이는 디버그 접근 보호 상태이며, 보호가 언제/누구에 의해 설정됐는지는 미확정이다. 사용자에게 기존 내용 삭제를 알렸고 사용자는 다른 사람이 사용한 보드라 깨끗하게 정리하라고 요청했다. 대상은4212 한 대로 제한했다.

`recover_4212.py`는 일련번호·전압·DP/CTRL-AP ID·기존 보호 상태·N3 HEX hash를 검사한 뒤 ERASEALL을 수행했다. 결과: 보호0→1, FICR_PART0x52840 / VARIANT0x41414630, N3 펌웨어135149바이트 전체 addressed HEX readback 일치, UICR.APPROTECT0x5A, 실행 및 새 연결에서 ID/UICR 재확인 PASS. 원본 프로그램/UICR/RAM은 삭제됐고 보호 상태여서 백업하지 못했다. 다른 보드에 ERASEALL을 하지 않았다.

근거: [Nordic nRF52840 debug/CTRL-AP](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/dif.html), [Nordic improved APPROTECT 설명](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/working-with-the-nrf52-series-improved-approtect). 보호 상태0은 활성,1은 비활성이고 전체 복구는flash/UICR/RAM을 지운다. 이 진단은 기존888의 RF/PER 원인 규명이 아니다.

원격에 별도 `exp4-rx-error-diag-20260904` worktree와 동일 바이너리를 배치했다. GitHub push는 하지 않았다. 이후 실험은 새4212의 기준을 새로 확보하는 별도 조건으로 기록한다.
