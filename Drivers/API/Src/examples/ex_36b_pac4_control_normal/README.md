# PAC4 Control Transmitter

이 송신기는 `ex_36a_pac4_control_init`과 한 쌍으로 사용한다.

프레임과 RF 도착 시각을 BRRS 조건과 동일하게 통제하기 위해 SYNC timestamp
기반 delayed-TX를 유지한다. PAC는 수신기 설정이므로 DATA의 PAC4 비교 효과는
주로 INIT/RX 노드에서 발생한다.

`example_selection.h`에서 `TEST_PAC4_CONTROL_NORMAL`만 활성화해 빌드한다.
