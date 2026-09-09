# 집 건조기 배치 · USB 재확인 후 PAC8 추가 1회

2026-09-07 14:00:18–14:00:45 KST(측정 제어·플래시 검증 포함), H8_r1. 요청한 추가 1회를 완료했다. 세 TX 모두 비컨 수신·송신 시도·송신 성공1000회, 시스템 및 수집 무결성 PASS다. 데이터 손실이 발생했으며 모든 노드 PER<1% 목표는 실패했다.

## 환경·설정

TX 3개는 집 건조기 안, INIT/RX는 건조기 뒤. TX USB 허브는 외부 전원 어댑터가 연결된 상태(사용자 확인 유지). SSH `s-macbook-air` 정상, 지정한 네 serial만 인식. 이전 실행의 USB 연결이 정상이 아니었다는 사용자 신고는 이전 폴더의 `USER_USB_ISSUE_ADDENDUM.json`에도 기록했고, 원시 로그를 보존한 채 유효 결과에서 제외했다. 이번 결과와 합산하지 않는다.

M32/PAC8/S3/G250, lead25 µs, SB/SP=3000/2500 µs, SF10 ms×1000, 슬롯347 µs, RX창122 µs/FWTO119 UUS. 직전과 동일 P25 INIT 이미지(슬롯별 bounded scheduled delayed-RX, SPI 최적화)와 보존된 세 TX 이미지를 사용했다. 역할 변경·재전송·새 빌드·코드 수정 없음.

실제 USB 구성: 로컬 INIT@00100000; 원격 USB2.1 Hub@01100000 → N2@01130000, N3@01140000, N4@01120000. 측정 직전·직후 registry ID, 포트, locationID, serial이 모두 동일했다. 물리 배치·방향·케이블·전원 변경을 수행하지 않았다.

## 노드별 결과

| 노드 | Serial | Offered | RX | Lost | PER |
|---|---|---:|---:|---:|---:|
| N2 | 1050211584 | 1000 | 917 | 83 | 8.3% |
| N3 | 1050273888 | 1000 | 937 | 63 | 6.3% |
| N4 | 1050282818 | 1000 | 876 | 124 | 12.4% |
| 전체 | — | 3000 | 2730 | 270 | 9.0% |

실제 TX 성공1000회씩을 확인한 유효한 1회 관측이다. N2/N3/N4 각각1%를 넘었다. 이전 NLOS6.9m 또는 다른 집 배치와 동일한 채널이라고 가정하거나 합산하지 않는다.

## TX·오류·종료 검증

- 모든 TX: beacon received1000/miss0/gaps0/duplicates0, attempt1000/success1000/delayed-TX late0, SYNC loss0, RX error0, config error0, 무선 END 수신1.
- INIT RX 오류270회: **SFD timeout268, PHR error1, RXFSL1, CRC error0**. FWTO0/PTO0/FINT-only0/overrun0.
- 슬롯별 RX error: N2=83, N3=63, N4=124. 오류 subtype은 전체 합계이며, 노드별 subtype을 별도로 추정하지 않았다. RX good+error=1000씩, timeout0.
- 모든 슬롯: attempted1000/armed1000/delayed-RX late0. delayed-SYNC late0, TX-wait timeout0.
- wrong length/slot/superframe0. source-to-slot 분류는 N2→slot0=917, N3→slot1=937, N4→slot2=876으로 총2730건 일치하며 잘못된 source/slot 조합 없음.
- RDB mismatch/incomplete/recovered/resync/overrun 모두0. deferred overflow/rearm deadline miss0.
- SPI begin/end1000/1000, direct transfers103344; begin/end/device/state/transfer/timeout/recovery 오류0.
- 네 보드 각각 RTT READY1/END STATS1, 캡처 exit0, 수집 timeout 없음. 각 TX 무선 END 수신도1.
- 네 HEX SHA256 확인 및 실플래시 populated-byte readback PASS. 엄격 audit: valid=true, collection/system=PASS, goal=FAIL_PER.
- 측정 직전·직후 로컬/원격 Git branch·HEAD·dirty·tracked diff SHA 동일. 잔여 캡처 프로세스 없음. commit/push 없음, GitHub 업로드 보류 유지.

## 해석

USB 재확인 후 이번에는 정상 송신을 전제로 한 데이터 손실을 확인했다. 따라서 집 건조기 배치에서도 M32/PAC8 손실 문제가 관측된다. 다만 N3만 나빴던 기존 NLOS 실행과 달리 이번에는 세 노드 모두 손실이 있고 N4가 가장 높다. 오류도 RXFSL 중심이었던 과거와 달리 SFD timeout268/270회가 대부분이다. 동일한 채널이나 동일한 단일 원인이라고 결론낼 수 없다.

이미 슬롯별 scheduled delayed-RX를 사용하는 펌웨어에서 발생한 결과이므로, continuous RX만 제거하면 어떤 배치에서도 PER<1%가 보장된다는 해석은 지지하지 않는다. SFD timeout만으로 잡음·차폐·PAC·타이밍 중 하나를 원인으로 확정하지 않는다. 추가 RF 실행이나 설정 변경은 하지 않았다.

## 역할·HEX SHA256

| 역할 | Serial | SHA256 |
|---|---|---|
| init | 1050270933 | `9840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373` |
| N2 | 1050211584 | `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20` |
| N3 | 1050273888 | `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651` |
| N4 | 1050282818 | `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1` |

## 증거 파일

- `H8_r1/init/init.log`, `H8_r1/tx/N2.log`, `N3.log`, `N4.log`: 원시 RTT.
- `H8_r1/audit.json`: 모든 무결성 검증과 노드별 PER.
- `H8_r1/manifest.json`, `init_flash_readback.json`, `tx_flash_readback.json`: 실행 조건·HEX·readback.
- `RESULTS.json`: TX/오류/종료 마커를 포함한 전체 결과.
- `local_preflight.json`, `remote_preflight.json`, `local_postflight.json`, `remote_postflight.json`, `postflight_verification.json`: USB·serial·Git·프로세스 증거.
