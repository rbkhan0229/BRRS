# 차량 MacBook Air 단독 수집 준비

2026-09-08. 사용자 승인: RX를 TX 노트북에 연결하고 연구실에서 SSH로 제어할 수 있도록 준비.

## 완료

- 새 `brrs_single_host.py`를 독립 차량 SDK 및 맥북에어의 새 캡처 번들에 배포.
- 한 호스트의 실제 J-Link serial 7개 전체 집합 확인 후, TX 6개 READY를 순서대로 확인하고 INIT/RX 시작.
- `start`는 SSH와 분리된 프로세스를 실행. 해당 RF 작업의 입력·출력·로그·상태·정지 처리는 맥북에어 내부에 있음.
- SSH 종료 후 3초 뒤 파일을 쓰는 무선 없는 검증을 실제 맥북에어에서 수행하고, 새 SSH 연결로 완료 증거 확인.
- 8개 무선 없는 테스트 통과: READY 순서, TX 실패 시 RX 완주 유지, READY 실패 시 RX 시작 차단, STOP 시 자식 정리, 보드 누락 차단, serial 오류 차단, 변조된 인덱스 차단, RX 0개 PASS 금지.
- 기존 7개 HEX/ELF 및 수집 스크립트의 `--no-build --build-only` 검증 통과. firmware C/HEX 수정 및 재빌드 없음.
- 자동 반복 없음. 사용한 번들을 재시작하거나 덮어쓰지 않음. 별도 `start` 요청 때 1회 실행.
- 작업 종료 시 각 보드 정지 및 flash readback, TX RAM 계수 보존. 한 보드가 분리되어도 다른 보드의 정지는 계속 시도.
- 실패한 노드의 원시 로그도 보존. END 실패를 정상 수집으로 바꾸지 않음.

## 현재 남은 현장 확인

- 21:05 KST 확인: SSH 정상, 배터리 83%, J-Link **0대**, 다른 수집 프로세스 없음. 보드 연결/정리 중으로 판단하며 정상 연결을 주장하지 않음.
- RX와 TX 허브를 맥북에어에 연결하고 아래 7개 serial이 모두 인식되어야 함. 이후 `check`, `park`로 전체 이미지 일치와 정지 상태를 검증.
- 노트북 덮개 열어 두기. 자동 화면 꺼짐은 허용됨. 4시간짜리 idle sleep 방지 PID 90409를 21:05:17 KST부터 실행(약 9월 9일 01:05까지). 실제 캡처 실행 중에도 별도 caffeinate가 작업 종료까지 적용됨.
- 연구실로 이동한 뒤에도 맥북에어에 인터넷이 남아야 함. 가져가는 휴대폰의 핫스팟이면 원격 접속 불가. 진행 중 수집은 인터넷이 끊겨도 완료 후 로컬 저장되지만, 새 실행/상태 조회에는 접속 복구 필요.
- 시동을 끈 뒤 USB 허브 전원 유지 여부 확인. 시동/문 상태는 재확인 전 `unknown`으로 기록. 이전 engine-on 실험과 같은 조건으로 취급하지 않음.

## 현재 준비된 조건과 역할

비컨 M512/PAC8, DATA M32/PAC8, lead 26us, TX 6대, 13슬롯 `2345672345673`, G250, SB/SP 3000/2500us, 1000SF. 슬롯별 delayed RX, 재전송 없음. 현재 단독 수집 진입점은 **Exp4용**이며 다른 단계용이라고 주장하지 않음.

| 역할 | Serial | 마지막 확인한 물리 위치 |
|---|---|---|
| INIT/RX | 1050270933 | 운전석/조수석 사이 수납함 위 |
| N2 | 1050211584 | 뒷범퍼 |
| N3 | 1050273888 | 대시보드 |
| N4 | 1050282818 | 운전석 |
| N5 | 1050208509 | 조수석 |
| N6 | 1050227627 | 트렁크 A |
| N7 | 1050204212 | 트렁크 B |

새 배치 변경은 별도 확인 필요. USB 호스트가 모두 `s-macbook-air`라는 사실은 manifest에 명시했고, 물리 위치는 마지막 사용자 설명만 유지함.

## 운영 명령 (맥북에어에서 실행)

`check`: 읽기 전용 연결/해시 점검. `park`: exact serial 정지/flash 읽기, reset/flash/RF 재실행 없음. `start`: 현재 번들로 RF 1회 시작. `status`: 상태와 요약. `stop`: 해당 번들만 종료 요청. 시작한 뒤 자동 재시도하지 않음.

```sh
python3 /Users/songchieon/Desktop/DWM3000/logs/vehicle_single_host_ready_20260908/capture1/sdk/Drivers/API/brrs_single_host.py check --bundle /Users/songchieon/Desktop/DWM3000/logs/vehicle_single_host_ready_20260908/capture1 --expected-index 744180036bc9221d54bffc51427da1e0b2d90b9dcb520cdfefaff31a88387ac5
```

필요한 동작에 맞춰 `check` 한 단어만 `park/start/status/stop`으로 변경. SSH 별칭은 `s-macbook-air`, 실패 시 `100.115.225.85`. 실행 전 실제 시동·문·전원·위치 기록을 확정하면 manifest와 payload index를 다시 생성하고 재배포해야 함.

결과: 맥북에어 `capture1/supervisor.log`, `capture1/results/status.json`, `capture1/results/SUMMARY.json`, `capture1/logs/`.

임시 4시간 잠자기 방지를 일찍 해제하려면 `idle_sleep_guard.json`의 PID가 여전히 해당 `caffeinate -i -t 14400`인지 확인한 뒤 그 프로세스만 종료. 관련 없는 caffeinate를 종료하지 않음. 영구 pmset 설정은 변경하지 않았음.

## 한계

보드가 연결되지 않아 실제 7개 동시 RTT/RF 검증은 아직 하지 않음. firmware의 기존 100ms 비컨 동기 상실 후 조기 종료 정책은 유지됨. 이번 작업은 호스트 수집 방식만 바꾸며 RF 성능 개선을 주장하지 않음. 기존 Git 저장소 변경/commit/push 없음.
