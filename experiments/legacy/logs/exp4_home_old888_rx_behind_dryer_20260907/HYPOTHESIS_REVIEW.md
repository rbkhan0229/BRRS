# M32 delayed-RX / manual burst 가설 검토 — 2026-09-07

결론: 슬롯별 scheduled delayed-RX A/B는 검토할 가치가 있지만, “현재 continuous-RX가 확정 원인이며 delayed-RX이면 프리앰블 검출이 제거된다”는 주장은 근거를 넘어선다. 이번에는 이미 delayed-RX로 시작하는 첫 DATA 슬롯 N2에서 45.4% 손실이 나고 후속 N3/N4는 0%였다.

1. 이번 관측

TX는 건조기 안, RX/INIT는 건조기 뒤(사용자 배치). 원래 역할 INIT1050270933 / N2=1050211584 / N3=1050273888 / N4=1050282818. 정확한 과거 G250 HEX로 1회, M32/PAC8/S3/lead15/SB3000/SP2500/1000SF.

N2 RX546/1000, PER45.4%; N3와 N4 RX1000/1000, PER0%. 전체 RX2546/3000, PER15.1333%. 세 TX 모두 beacon/attempt/success=1000/1000/1000. INIT SFD timeout454, PHR/CRC/RXFSL/FWTO/PTO=0. wrong slot/superframe, delayed-RX/TX late, RDB mismatch/incomplete/recovered/resync/overrun=0. 종료/수집/해시 검증 정상. 독립 wrong-source 및 현대 SPI fault counters는 옛 HEX에서 미제공이다.

기존 NLOS 6.9m의 N3 약48.7–55.1%/RXFSL 우세와 이번 N2 45.4%/SFD timeout 전부는 다른 노드·오류 구성을 보인다. “M32에서 특정 노드만 크게 잃는 현상”은 얻었지만, 사전 N3≥10% 기준은 충족하지 않았고 기존과 동일 채널·동일 원인이라고 판정하지 않는다. 추가 반복은 사용자가 1회로 제한하여 하지 않았다.

2. 실제 펌웨어 동작

실행 raw init.log 11행은 data_rx=delayed_first_manual_double_buffer_burst, 14행은 owner=234를 명시한다. N2가 첫 슬롯(slot0)이다. 62행의 성공 N2 표본은 RX-open→RMARKER=57us(546 samples), 64행의 SYNC→RMARKER=3000us다. 실패 패킷의 RX-open/검출 상세를 성공 표본에서 외삽하지 않는다.

현재 분석 트리 55a23fa의 brrs_init.c:2392 schedule_delayed_rx는 DX_TIME을 설정하고 DWT_START_RX_DELAYED|DWT_IDLE_ON_DLY_ERR로 첫 창을 연다. Exp4는 dwt_setrxtimeout(0)으로 burst 전체에서 FWTO를 끈다. :2623 schedule_rx_slot은 slot RMARKER에서 PREAMBLE+SFD+lead를 뺀 시각을 사용한다. :3097 exp4_rearm_after_event는 다음 DATA 슬롯이 있을 때 CMD_RX(즉시 RX)를 발행한다. 프레임 수신/오류 후 칩 수신을 수동 재개하므로 무중단 RX나 RXAUTR 자동 재수신과 정확히 같은 뜻은 아니다. 비수신 구간 절전은 유지된다.

현재 소스를 이번 HEX의 정확한 소스라고 주장하지 않는다. 과거 후보 commit90cdffb의 :1857–1906 및 :2270–2301에서도 동일한 큰 구조가 확인되며, 이 후보의 정확한 바이너리 provenance는 기존 기록대로 미확정이다. 실제 배포 바이너리 동일성은 지정 SHA256과 네 보드 populated flash bytes readback으로 확보했다. 소스 후보는 read-only git show로 별도 .txt에 보존했다.

이번 결과는 “앞 DATA 패킷 처리 뒤 두 번째/세 번째 슬롯에서 길게 탐색한 것이 모든 고손실의 필요 원인”이라는 좁은 설명과 맞지 않는다. 다만 첫 슬롯에서도 오류가 먼저 나면 후속 오류 복구가 개입할 수 있고, 이전 SF/비컨 PHY 전환·RX 진입 상태의 영향도 있어 모든 소프트웨어/수신 이력 영향을 배제하지는 않는다.

3. 논문의 문맥

0330-09-ko.pdf, PDF2쪽 III-A는 프리앰블 검출을 제거 가능하다고 서술하고, III-B는 채널 추정 단독으로 30–33dB면 충분할 것으로 “추정”한다. 그러나 같은 2쪽 IV 도입부는 II–III를 이상적 수신기 전제의 분석이라고 명시한다. 3쪽 실험3은 DWM3000에서 SFD/PHR를 생략할 수 없다고 설명하며, 실험4는 진정한 BRRS 구현이 아니라 프리앰블 축소 검증이라고 구분한다. V에서도 현재 DWM3000의 전체 SHR/PHR 파이프라인과 이상적 포맷의 차이를 인정한다.

따라서 이론적 검출 제거를 현재 DW3000의 CMD_DRX 호출과 동일시하면 안 된다. 논문이 scheduled 수신 실험을 의도한다는 문제제기는 타당하며, 후속 슬롯까지 각각 예약하는 대조군은 방법론을 더 직접적으로 시험한다. 그러나 프로토콜 전제 위반→고손실 확정이라는 인과 결론은 이 문맥과 실제 실험만으로 성립하지 않는다.

4. DW3000 delayed-RX는 검출 생략이 아니다

로컬 DW3000-User-Manual.pdf v1.1, §4.3(PDF41–42쪽)는 RX를 켤 시각까지 IDLE_PLL에 머문 후 수신기를 켠다고 설명한다. §8.2.7.3(PDF147쪽)는 CMD_DRX를 쓴 경우에도 예약 시각에 수신기가 켜진 뒤 프리앰블 탐색 및 검출 timeout이 시작된다고 명시한다. §8.2.7.1–2(PDF146쪽)는 프리앰블 획득 후 복조를 시도하며, 검출에 PAC 한 묶음이 소모된다고 설명한다. SDK dw3000_device.c:5439–5455도 immediate 모드는 CMD_RX, delayed 모드는 CMD_DRX를 선택한다.

즉 CMD_DRX는 사전 무신호 탐색 시간과 전력을 줄이는 수단이며, 칩의 preamble/PAC 획득 또는 SFD/PHR 경로를 우회하는 스위치가 아니다. 매 슬롯 delayed-RX를 적용해도 이 표준 수신 경로는 남는다. M32에서 충분한 획득/동기화/CIR 품질이 나오는지는 별도 측정 문제다.

5. SFD timeout454의 의미

매뉴얼 §8.2.7.2(PDF146쪽)와 RXSTO 설명(PDF98쪽)에 따르면 프리앰블 검출 뒤 제한 시간 내 SFD가 잡히지 않으면 수신을 중단한다. 따라서 이번 결과를 단순히 “프리앰블을 전혀 못 찾았다”라고 부르지 않는다. 거짓 프리앰블 검출, 실제 신호의 불완전 획득/SFD 검출 실패, 진입 타이밍·상태 상호작용 등이 후보로 남는다. 현재 합산 카운터만으로 실제/거짓 검출을 구별할 수 없다. 이 실행에서는 N2 누락454와 SFD timeout454가 수치상 일치하지만 개별 사건 매칭을 기록한 것이 아니므로 1:1 인과 대응으로 확정하지 않는다.

6. 다음 A/B의 가치와 판정

동일 M32/PAC8, 같은 배치·역할·전원·HEX 기반 설정, 재전송 없음, TDMA·비수신 절전을 고정하고 현재 burst와 슬롯별 scheduled delayed-RX를 교차할 가치는 있다. 그 목적은 탐색 구간/수신 재진입 이력의 영향을 측정하는 것이며 검출 회로를 제거하는 것이 아니다.

다만 이번 고손실은 이미 delayed-RX인 첫 슬롯이므로, 후속 슬롯만 바꾸면 N2가 개선된다는 예측은 바로 나오지 않는다. 첫 슬롯의 수신 창과 초기화 경로는 양쪽에서 같게 하고, N2/N3/N4를 따로 판정해야 한다. 첫 슬롯도 개선되면 이전 SF 상태·timeout/복구·버퍼 상태까지 어떤 변화가 같이 들어갔는지 확인해야 한다. 현재 세팅의 반복 안정성을 먼저 확인하면 좋지만 이번에는 추가 RF 실행 없이 종료했다.

SFD timeout 종류와 발생 시점, RXPRD/RXSFDD 전후 상태, 예약 RX 시각과 실제 TX RMARKER의 관계를 구분해 보존하는 것이 후속 진단의 우선 과제다. 진단 read가 제어 흐름이나 슬롯 추정을 바꾸지 않게 설계해야 한다. M64/PAC/출력/위치 변경으로 우회하지 않는다. 구현, commit, push는 하지 않았다.

자료: 로컬 논문과 Qorvo/Decawave 매뉴얼의 관련 페이지를 텍스트 추출하고 렌더링해서 확인했다. 공개 매뉴얼 사본의 검색 결과도 §8.2.7.3 문구와 일치했다: https://caramelfur.dev/docs/DW3000-User-Manual.pdf . 공식 Qorvo da008154 링크는 현 시점 제품상태 안내 페이지로 돌아가 원문을 받지 못했으므로 로컬 원문을 주 근거로 삼았다. 커뮤니티 게시물은 원인 확정의 근거로 사용하지 않았다.
