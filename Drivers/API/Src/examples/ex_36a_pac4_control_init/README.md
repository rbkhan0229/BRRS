# PAC4 Immediate-RX Control

이 폴더는 BRRS delayed-RX + PAC8 조건과 비교하기 위한 수신 대조군이다.

## 통제 조건

- DATA preamble: 32 symbols
- DATA PSDU: 127 bytes
- TX power, channel, SFD, PHR, SYNC preamble, period, slot start: BRRS 실험과 동일
- Normal 노드의 delayed-TX: 동일한 RF 도착 시각을 만들기 위해 유지

## 비교 변수

- BRRS 조건: 예약 delayed-RX, PAC8
- 대조군 조건: 패킷 도착 전 immediate RX, PAC4

따라서 이 대조군에서 "BRRS 미사용"은 코디네이터의 예약 delayed-RX를 사용하지
않는다는 뜻이다. TX의 delayed-TX까지 제거하면 패킷 도착 시각과 지터가 함께
달라져 PAC 효과와 혼합되므로 유지한다.

## 선택

`example_selection.h`에서 한 번에 하나만 활성화한다.

- RX 노트북: `TEST_PAC4_CONTROL_INIT`
- TX 노트북: `TEST_PAC4_CONTROL_NORMAL`

시작 로그에서 다음 설정을 확인한다.

`PAC4 CONTROL: ... RX=IMMEDIATE PAC=4 ...`
