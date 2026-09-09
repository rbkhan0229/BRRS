# 집 TX 건조기 안·RX 선반 위 — PAC8 1회 PASS

2026-09-07 14:05:48–14:06:14 KST(측정 제어·플래시 검증 포함), H8_r1. 요청한 추가 1회를 완료했다. 모든 TX가 비컨 수신·송신 시도·송신 성공1000회, INIT가 노드별1000개씩 총3000개를 수신했다. **이번 1회 관측에서 각 노드 PER0%, 목표<1% 충족.**

## 환경과 고정 조건

사용자가 RX를 건조기 뒤에서 선반 위로 옮겼다. TX 세 보드는 건조기 안, 허브 외부 전원 연결은 이전 사용자 확인을 유지한다(그 밖의 변경 신고 없음). 새 배치로 구분하며 이전 RX 건조기 뒤 및 NLOS6.9m 결과와 합산하지 않는다.

M32/PAC8/S3/G250, lead25 µs, SB/SP=3000/2500 µs, SF10 ms×1000, 슬롯347 µs, RX창122 µs/FWTO119 UUS. 직전과 같은 P25 INIT(슬롯별 bounded scheduled delayed-RX, SPI 최적화)와 보존된 세 TX HEX다. 재전송·새 빌드·코드 수정 없음.

SSH `s-macbook-air` 정상. 로컬 INIT1050270933@00100000; 원격 USB2.1 Hub@01100000 → N2=1050211584@01130000, N3=1050273888@01140000, N4=1050282818@01120000. 지정된 네 보드만 인식 및 플래시했다. 측정 전후 registry ID·포트·locationID·serial 동일, 실험 중 물리 배치·케이블·포트·전원 변경을 수행하지 않았다.

## 노드별 결과와 직전 배치 비교

| 노드 | Serial | Offered | RX | Lost | 이번 선반 위 PER | 직전 건조기 뒤 PER |
|---|---|---:|---:|---:|---:|---:|
| N2 | 1050211584 | 1000 | 1000 | 0 | 0.0% | 8.3% |
| N3 | 1050273888 | 1000 | 1000 | 0 | 0.0% | 6.3% |
| N4 | 1050282818 | 1000 | 1000 | 0 | 0.0% | 12.4% |
| 전체 | — | 3000 | 3000 | 0 | 0.0% | 9.0% |

각 배치1회씩의 관측을 나란히 제시한 것이며 합산 결과가 아니다. 직전 결과는 `../exp4_home_dryer_pac8_usb_recheck_20260907/RESULTS.md`에 보존돼 있다. 그보다 앞선 USB 이상 실행은 비교에서 제외했다.

## 오류·종료·무결성

- 모든 TX: beacon received1000/miss0/gaps0/duplicates0, attempt1000/success1000/delayed-TX late0; SYNC loss0, RX error0, config error0, 무선 END 수신1.
- INIT SFD timeout/PHR error/CRC error/RXFSL/FWTO/PTO/FINT-only 모두0.
- 각 슬롯 attempted1000/armed1000/RXgood1000/timeout0/error0/late0. delayed-SYNC late0, TX-wait timeout0.
- wrong length/slot/superframe0. source-to-slot 분류는 N2→0, N3→1, N4→2 각1000건, 잘못된 조합 없음.
- RDB mismatch/incomplete/recovered/resync/overrun 모두0. deferred overflow/rearm deadline miss0.
- SPI begin/end1000/1000, direct transfers103916. SPI 오류·timeout·recovery0.
- 네 보드 각각 RTT READY1/END STATS1, 캡처 exit0, 수집 timeout 없음. 각 TX 무선 END 수신도1.
- 네 HEX hash와 실플래시 populated-byte readback PASS. 엄격 audit valid=true, collection/system/goal PASS.
- 로컬/원격 Git branch·HEAD·dirty·tracked diff SHA 유지. 잔여 캡처 프로세스 없음. 추가 RF 실행·commit·push 없음, GitHub 업로드 보류 유지.

## 해석

동일 펌웨어로 RX를 선반 위에 둔 이번 실행에서는 직전의 고손실이 관측되지 않았다. 배치에 따른 채널 변화가 성능에 영향을 주었을 가능성을 지지하지만, 각1회씩이고 시간 변화도 있으므로 개선 원인을 이동 하나로 확정하지 않는다. 약10초·노드당1000패킷의 성공이며 장기 PER 보장이나 기존 NLOS6.9m 문제 해결을 의미하지 않는다. 물리 위치 변경을 원래 고정 배치의 펌웨어 해결책으로 간주하지 않는다.

## 역할·HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `9840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

## 보존 자료

`H8_r1/init/init.log`, `H8_r1/tx/N2.log`, `N3.log`, `N4.log`에 원시 RTT가 있다. `H8_r1/audit.json`에 엄격 검증, `H8_r1/manifest.json`에 실행 조건·hash, `init_flash_readback.json`/`tx_flash_readback.json`에 readback을 보존했다. `RESULTS.json`에 전체 카운터·마커·비교를 저장했다. 전후 로컬/원격 preflight/postflight와 `postflight_verification.json`에 USB·Git·프로세스 증거를 남겼다.
