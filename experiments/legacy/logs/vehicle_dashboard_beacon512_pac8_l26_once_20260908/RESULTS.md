# 차량 대시보드 RX — 비컨 M512 1회 결과

**비컨 512만으로 범퍼 연결이 해결되지 않았다. RX 1,000SF는 완주했으며, 모든 노드가 예정 전송 기준 PER <1% 목표에 실패했다.**

2026-09-08 17:57:38–17:59:02 KST 실행. 실제 RX 수집은 TX 6대 READY 확인 후 시작해 1,000SF를 완주했다. N2 수집 timeout과 N3 END 비컨 미수신으로 7대 전체 수집 판정은 INCOMPLETE/FAIL이다. RX 최종 수신 통계와 N4–N7의 완전한 TX 통계는 확보했다.

## 조건과 변경

최신형 현대 코나, RX 대시보드 위. 앞범퍼 좌우 플라스틱 바깥 N2/N3, 운전석 N4, 조수석 N5, 트렁크 N6/N7. 시동 켜짐·외부 전원 허브·모든 문 닫힘은 사용자 확인 상태를 유지한 것으로 기록했다. 이번 요청에서 위치/방향 변경 없음.

DATA M32/PAC8·lead 26µs, G250, SB/SP=3000/2500µs, TX 6대·13슬롯(`2345672345673`), 10ms 주기, 1,000SF, 슬롯별 delayed-RX, 재전송 없음. DATA 수신창 123µs/FWTO 120UUS 유지.

독립 SDK 복사본에서 INIT/NORMAL의 `SYNC_PLEN`과 `SYNC_PREAMBLE_SYMBOLS`만 512로 변경했다. 비컨 PAC8은 유지. 파생값은 SFD timeout 513, RMARKER offset 530µs, 비컨 airtime 모델 598µs, RX early 542µs, RX window 610µs로 함께 바뀐다. 컴파일 성공 및 7개 ELF PHY 구조체 검사로 확인했고, 실행 로그에서 INIT sync_plen=512 및 TX 6대의 수신창 610µs를 확인했다. DATA PHY 구조체는 이전 256 ELF와 바이트 단위로 같다.

수집기 복사본은 한 TX가 조기 실패해도 다른 보드의 제한 시간 내 수집을 끝까지 기다리도록 조정했다. 검증 실패를 무시하거나 PASS로 바꾸지 않는다. 이번에는 N4–N7/RX 완주 후 N2 60초 timeout과 N3 실패가 최종 FAIL로 남았다.

## 노드별 결과

| 노드·위치 | 비컨 수신 | TX 시도 / 완료 | DATA RX / 예정 | 예정 전송 기준 PER |
|---|---:|---:|---:|---:|
| N2 앞범퍼 A | 0 | 0 / 0 | 0 / 2,000 | 100.00% |
| N3 앞범퍼 B | 28 | 84 / 84 | 0 / 3,000 | 100.00% |
| N4 운전석 | 1,000 | 2,000 / 2,000 | 1,807 / 2,000 | 9.65% |
| N5 조수석 | 1,000 | 2,000 / 2,000 | 1,886 / 2,000 | 5.70% |
| N6 트렁크 A | 1,000 | 2,000 / 2,000 | 1,946 / 2,000 | 2.70% |
| N7 트렁크 B | 1,000 | 2,000 / 2,000 | 1,452 / 2,000 | 27.40% |

예정 전송 기준 PER에는 비컨을 받지 못해 송신하지 않은 슬롯도 손실로 포함한다. N2는 송신 0회여서 송신 후 PHY PER을 정의할 수 없다. N3는 실제 84회 송신했지만 수신 0회로 송신 후 손실도 100%였다. N4–N7은 비컨 1,000/1,000, TX 2,000/2,000, END 비컨 수신 및 수집 검증 PASS이므로 표의 PER이 실제 DATA 손실률과 같다. 모든 TX delayed-late는 0.

전체 RX는 7,091/13,000, 예정 전송 기준 손실률 45.4538%. 실제 송신 8,084회 중 손실은 993회(12.2835%). 총 손실 5,909회는 미송신 4,916회와 송신 후 미수신 993회로 분해된다. 전체 평균으로 노드별 실패를 감추지 않는다.

N3는 비컨 28개를 수신한 뒤 조기 종료했다(마지막 seq=80, gap=52, sync loss timeout=16, beacon RX 오류=61, END 미수신). 이는 정상 1,000개를 끝까지 기다려 측정한 비컨 PHY PER이 아니다. N2는 END 통계 없이 60초 수집 timeout; 정지 후 리셋 없는 RAM에서 비컨/attempt/success 모두 0을 확인했다. N2 누계 RX 오류 227은 대기 전체 구간의 값으로 RX의 10초 DATA 오류와 합산하지 않는다.

## RX 및 시스템 오류

| 항목 | 횟수 |
|---|---:|
| SFD timeout | 910 |
| PHR error | 3 |
| CRC error | 0 |
| RXFSL | 7 |
| FWTO / PTO | 4,989 / 0 |
| wrong slot / superframe / length | 0 / 0 / 0 |
| delayed-RX / 비컨 delayed-TX late | 0 / 0 |
| RDB mismatch / incomplete / recovered / resync / overrun | 0 / 0 / 0 / 0 / 0 |
| DATA config / rearm deadline / deferred overflow / SPI errors | 모두 0 |

wrong source는 독립 카운터가 로그에 없어 별도 수치로 단정하지 않는다. 13개 슬롯 각각 1,000회 수신창을 열었으며 INIT의 END 통계를 확보했다. RX의 정상 수집 판정은 링크 목표 통과를 뜻하지 않는다.

## 직전 256 결과와 해석

직전 대시보드 M256은 조기 중단되어 N4/N5/N6/N7의 비컨 누계가 311/424/536/649, 송신 완료 누계가 622/848/1,072/1,298이었다. 보드마다 중단 시점이 달라 PER을 계산하거나 이번과 동등 구간으로 비교하지 않는다.

N2는 256과 512에서 모두 비컨 수신/송신 0회. N3는 직전 3개·9회에서 이번 28개·84회가 관측됐지만 관찰 길이와 조기 종료가 달라 비컨 개선 배율로 해석할 수 없다. 이번에도 범퍼 DATA 수신은 0이며, 512만으로 링크를 확보하지 못했다. N4–N7은 비컨을 모두 수신하고 예정대로 송신했지만 DATA 손실이 남아 있다. 비컨 연결 문제와 DATA 수신 문제를 각각 다뤄야 한다.

현재 수신은 이미 슬롯별 bounded delayed-RX다. 이번 오류를 continuous RX 단일 원인으로 설명할 수 없다. RXFSL 등의 오류만으로 금속 차폐나 특정 방향을 단일 원인으로 확정하지 않는다. 직전 256 DATA PER이 없으므로 512가 DATA 성능을 악화시켰다고 결론내리지 않는다.

## 역할·HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `d48615c9cd6a3dcce4ab1d0785b644f8c259e3af72ff9786dd3690f0cc8aeb00` |
| N2 | 1050211584 | `dd0fa35df59fa67eec32880da060500d24d5a9a5eca7eb1ef628eb0550094cf6` |
| N3 | 1050273888 | `c4b726f9e20456056709b85522662ecefa347e9d673681f5bb01f997391cd455` |
| N4 | 1050282818 | `998d287b827cec0cdde7a3c4c590f3fca8cb0687e188792894e9910b3b9eec57` |
| N5 | 1050208509 | `892c271d77059275efa26614017f32fd87534f352a425ca9f7db21d4b1a0bcef` |
| N6 | 1050227627 | `97483fbf34ed5e5fdf0b52fe4bbc1cb8656c8614717e571319c8753ef02e32fe` |
| N7 | 1050204212 | `2177124d049d709c5a0e2269d63eefd6e3b9d7ada996d1521c5a0764e623ed90` |

SSH와 정확한 serial 집합을 확인하고 해당 7대만 플래시했다. 사전 점검 중 INIT USB가 잠시 미인식됐지만, 플래시/RF 이전 재인식과 역할 검증 후 시작했다(`pre_rf_connection_note.json`). 추가 RF 실행 없음. 종료 후 7대 모두 플래시 readback PASS 및 halted를 확인했다. 로컬/원격 캡처 프로세스 잔여 0. 기존 Git 저장소 브랜치·HEAD·dirty 상태 변경, commit/push 없음. 현재 보드에는 512 시험 펌웨어가 남아 있고 정지되어 있다.

## 보존 자료

- `RESULTS.json`: 카운터·판정·노드별 원시 로그 해시
- `manifest.json`, `image_validation.json`, `deployment.json`: 설치 기록·ELF 설정 검증·배포
- `changes.patch`: 독립 복사본의 펌웨어 및 수집기 변경
- `capture1/results/local/init.log`: 완전한 RX 원시 로그
- `remote_capture_files/`: 모든 TX 원시 로그와 생성된 metadata
- `capture1/results/{local,remote}/status.json`, `orchestration.json`: 감독 수집 상태
- `recovery_*.json`: 실제 플래시 및 정지·RAM 검증
- `preflight_*.json`, `postflight_*.json`: 연결·프로세스·Git 검사
