# NLOS 6.9m M32 burst / slotted RX A/B

사용자 승인: 수신 창의 긴 탐색에서 잡음/거짓 획득 영향을 의심하며 A/B 제안. 기존 소스 사본에서 구현하고 A→B→A→B, 각1000SF. 위치·방향·허브 전원·포트·PHY·TX 출력은 변경하지 않는다. INIT933 / N2=584 / N3=888 / N4=818. 외부 전원 연결 허브, 현재 NLOS6.9m는 사용자 확인 조건이다.

A: HEAD55a23fa의 현재 burst RX, 기존 정상·오류 처리 유지. B: 같은 소스의 BRRS_EXP4_SLOTTED_RX=1. TX는 두 조건 모두 정확한 8월25일 HEX. M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF, 재전송 없음. A를 최종 소스에서 재빌드해 첫 실행 A와 HEX byte 동일성을 검증했다.

B는 첫 슬롯을 기존 절대 시각에 예약하고 FWTO110UUS(약112.82us)를 설정한다. 후속 슬롯은 DX_TIME/CMD_DRX/HPDWARN 순서로 예약하며 즉시RX fallback은 없다. 최종 B는 정상 수신 때 CIA 완료 확인 및 완료 프레임 FINFO/RX_TIME 저장 후 예약하고, 헤더 처리·buffer release를 이어간다. 오류는 상세 상태를 읽고 지운 다음 다음 슬롯의 절대 시각을 예약한다. 각 창은 한 수신 시도이며 정상/오류/timeout이 슬롯을 종료한다. PHY 설정·PLL·FWTO를 매번 다시 설정하지 않는다. 슬롯 사이 receiver idle, DATA 밖 RF off와 다음 비컨 scheduled RX 정책을 유지한다.

첫 B 시안은 일반 helper를 모든 패킷 처리 뒤 호출했다. 첫 A의 실제 전체 hot-path 최대284us 및 일반 RX 예약192us를 확인하여 G250에 맞지 않는 것으로 판단하고 RF 실행 전에 수정했다. 이 시안은 images/B_unflashed_v1에 보존됐고 실험 결과에는 사용하지 않았다. 실제 B는 images/B/init.hex 및 해당 run manifest로 특정한다.

유효성 기준: 네 역할 정상종료, 각TX beacon/attempt/success1000, 해시·flash readback 일치, wrong slot/SF/length/config/late/RDB/overrun/수집 오류0. B는 각슬롯 attempted=armed=1000,late0,good+timeout+error=1000 및 정상RX수와 각노드RX수 일치 확인. 유효RX0은 PASS 금지. 목표는 각 노드 PER<1%이며 전체 평균으로 대체하지 않는다. 유효성 실패 실행은 보존하고 PER 비교에서 제외한다.

이 A/B는 수신 창과 관련 제어·복구 방식 묶음의 효과를 본다. 좋은 결과가 나와도 잡음 누적 또는 특정 단일 내부 상태를 증명하지 않는다. A는 기존 post-rearm error-detail 읽기 구조여서 subtype이 fint-only로 소실될 수 있다. B는 예약 전 상세 상태를 보므로 오류 subtype 분포의 직접 비교는 제한된다. FWTO는 A에서 꺼져 있고 B에서 켜져 있으므로 FWTO 증가 자체를 감도 악화로 보지 않는다. 각 노드 offered/RX/PER가 핵심 비교다.

기존 repo branch/HEAD/dirty와 원격 기존 데이터는 보존한다. commit/push 없음. 실행 로그와 보고서는 이 폴더에 보존한다.

개발 중 B1은 wrong-length241/wrong-slot65로 무효 처리했다. 추가 예약 SPI 처리가 metadata 읽기를 다음 RX 시작과 겹치게 할 수 있어, metadata를 예약 전에 저장하도록 순서를 보완하고 후속 슬롯의 선택적 SYS_TIME 읽기를 제거했다. final B는 4ead1d189ff14ab6dfab55c4fb590d16affa639bdd23ecb303e8787eaf8a6f8f. 수정 뒤 A를 다시 빌드했으며 A1 HEX와 byte 동일하다. 최종 교차 비교 순서는 A2/B2/A3/B3이며 A1과 B1은 개발 단계 기록으로 분리한다.

최종 정리: B2/B3 뒤에는 종료 판정만 B window/terminal accounting에 맞게 수정했다. 최종 B SHA256은2234d3e8468572c9396e9b9abf108691400868f88eb66c43bde021be12df2f26. B4는 RX/수집 정상이나 N4 TX999회여서 제외. 최종 유효 비교는 comparison_order.json의 A4/B5/A5/B6이며 상세 결과는 RESULTS.md가 최신이다.
