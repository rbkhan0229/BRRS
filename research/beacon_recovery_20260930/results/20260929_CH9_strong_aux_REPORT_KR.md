# CH9 강한 AUX 조건 단일 비컨 누락 허용 비교

- Air 원본: `/Users/songchieon/Desktop/DWM3000/logs/home_beacon_holdover_power40_recheck3_20260929_191522`
- Ubuntu 사본: `/home/chieon/DWM3000/logs/home_beacon_holdover_power40_recheck3_20260929_191522`. 권위 있는 원본은 Air이며 원본 case/bundle, 봉인 payload 및 공식 판정은 유지했다.
- 자택 마지막 사용자 확인 배치: INIT–N7 안테나 기준 약 30 cm, AUX–INIT 약 1 m, USB/전원 및 주변 통행·작업 변화 없음(연속 계측 아님). 차량 측정이 아니다.
- CH9, DATA M32/PAC8/code11, 비컨 M512/code10, 10 ms 주기, N7(1050204212)→INIT(1050270933), lead 44 us **실내 임시값**. AUX(1050257038)는 M64/code9/1009 us, 출력 설정 index40, 네 case 모두 지속 ON. index64 대비 명목 +6 dB 설정 차이이며 방사 출력 실측이 아니다.
- 순서 OFF–ON–ON–OFF, 각 2000 SF/RF 정확히 1회, 재시도 없음. ON은 정상 비컨 연속 8개로 주기 추정 후 한 번의 비컨 누락만 예측 송신한다. 재전송/ACK 없음.

| 순서 | 비컨 수신 | 예측 송신 | 연속 누락 차단 | 실제 TX | DATA RX | 미송신 | 송신 후 손실 | offered PER | 공식 판정 |
|:--|--:|--:|--:|--:|--:|--:|--:|--:|:--|
| OFF① | 1953/2000 | 0 | 0 | 1953 | 1921 | 47 | 32 | 3.95% | FAIL_PER |
| ON① | 1943/2000 | 27 | 2 | 1970 | 1946 | 30 | 24 | 2.70% | FAIL_PER |
| ON② | 1904/2000 | 44 | 4 | 1948 | 1916 | 52 | 32 | 4.20% | FAIL_PER |
| OFF② | 1947/2000 | 0 | 0 | 1947 | 1904 | 53 | 43 | 4.80% | FAIL_PER |

OFF 두 회 합계 수신 3825/4000, PER 4.375%. ON 두 회 합계 수신 3862/4000, PER 3.450%. 이 네 관측에서는 ON이 offered 기준 **0.925%p 낮았고 DATA RX가 37개 많았다**. ON 비컨 미수신은 총 153개였으며 예측 송신 71회, 남은 미송신 82개다. 그러나 ON 자체의 비컨 수신·간섭 변동이 있었고 각 모드 2회뿐이다. ON이 공식 <1% 목표를 만족하지 못했으므로 강간섭 문제 해결이나 차량 적용을 주장하지 않는다. 두 paired 비교 모두 ON이 낮았지만 인과 효과 크기 확정도 아니다.

네 case 모두 공식 `FAIL_PER`(PER<1% 목표 실패)이고, 운영상 `>5%` 중단선은 넘지 않았다. 수집/metadata/READY/END/각 run readback 및 모든 보드 정지 3300 mV 통과. 비컨 trace overflow/truncation 0, predicted timing bad/late 0, AUX는 캡처 전체에서 TXFRS 증가 검증. 이것은 특정 DATA 프레임과 AUX 패킷의 충돌을 입증하지 않는다. 마지막 활성 INIT/N7/AUX 전체 HEX readback PASS, 8대 HALT, 수집 프로세스 0, root/global STOP 유지. `FINAL_HARDWARE_AUDIT.json`, `FINAL_PAYLOAD_AUDIT.json`, 각 case의 `finished.json`/`FINAL_AUDIT.json` 참조.

이번 요청 중 선행 격리 시도도 순서대로 보존했다. `home_beacon_holdover_power40_retry_20260929_184920`은 trace 저장량을 늘린 후 첫 OFF RF 1회가 검증기 prediction-check 고정 하한 때문에 공식 INVALID(원시 PER 4.05%는 공식값 아님)였다. `home_beacon_holdover_power40_recheck2_20260929_190335`는 OFF 공식 FAIL_PER 2.85% 이후 ON RF 1회가 연속 비컨 누락 차단 계수 2를 검증기가 반영하지 못해 INVALID(원시 PER 3.00%는 공식값 아님)였다. 두 root 모두 즉시 멈췄고 나머지 case 미실행, 원판정/STOP 보존. 이번 root의 새 검증기는 해당 선행 로그의 **복사본만** 읽기 전용 재검증해 PASS했으며 과거 공식 INVALID를 수정하지 않았다. 그 전 `home_beacon_holdover_power40_20260929_172200` 첫 OFF INVALID(trace overflow2)도 별도 보존한다.

실기 펌웨어는 앞선 index40 N7 archive-8192 OFF/ON 및 AUX index40 이미지를 그대로 사용했고 이번 root에서 펌웨어 재빌드 0. 격리 host 검증기/테스트 세 파일만 변경; Air 오프라인 165 tests PASS(기존 fixture 4 skip), 각 case 128 payload hash 및 소스 부모 불변 감사 PASS. Ubuntu RAM 오류 미수리로 Air에서 빌드·제어·수집했다. Exp4/Standard/Stage0 결과와 합산하지 않으며 lead를 차량 frozen 값으로 처리하지 않는다. 추가 RF 예약·자동 재개 없음.
