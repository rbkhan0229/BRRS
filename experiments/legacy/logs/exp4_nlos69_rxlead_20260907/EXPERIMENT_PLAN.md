# M32/PAC8 슬롯별 delayed RX의 lead sweep

목표: 각 노드 PER<1%. 유지: 현재 NLOS6.9m 배치/방향/전원/케이블/포트, M32/PAC8/S3/G250/SB3000/SP2500, 기존 TX HEX, 재전송 없음.

이번 단계에서는 INIT RX lead만 변경한다. 15,5,0,25us를 각1000SF 한 번씩 측정하고 15us 기준을 다시 측정한다. 창 종료 예정시각은 일정하며 시작만 변경된다. 순서 L15r1,L5r1,L0r1,L25r1,L15r2. 0us는 안정 여유가 부족할 수 있는 경계 검사이며 권장값을 가정하지 않는다.

후보 선택은 모든 노드의 최대 PER 기준이다. 좋아진 값은 새 반복 실행으로 확인하고, 한 번의 최저 결과만으로 성공 판정하지 않는다. 현재값보다 개선이 없으면 불필요하게 더 좁히지 않는다. 모든 실행과 실패를 보존한다. FWTO도 손실에 포함한다. 각TX1000attempt/success, 시스템/수집 오류0을 유효 비교 조건으로 유지한다.

소스는 이전 최종 B의 독립 사본이며 기존 저장소와 지난 결과는 수정하지 않는다. commit/push 없음. 새 HEX는 SHA와 RTT 심볼을 확인하고 명시한serial만 플래시한 뒤 populated bytes를 readback한다.

## 적응적 변경: L5 후
L5r1 RX0/3000, FWTO2761/PHYerror239, 모든 창 armed1000/late0. 원본 collectionFAIL 그대로 보존. 더 작은0us는 생략하고 L25r1,L15r2를 이어서 측정한다. RX0을 PASS로 허용하지 않는다.

## L25 후보 확인
L25r1 N2=0%,N3=0.6%,N4=0%, TX/수집/시스템검증PASS. 후보선정 실행과 별도로 L25r2,r3,r4 각1000SF를 연속실행해 재현성을 확인한다. L15r2는 계획된 기준재측정으로 유지한다. 단일최저치가 아닌 확인실행 각각 및 합산을 보고한다.

## L25r2 구조적 실패 후 SPI 처리 비교
L25r2 N3PER0.5%, N4PER0.1%. 그러나 RDB/CIA readiness recovery1회로 metadata처리최대179us 및 다음N4예약late1회. collectionFAIL 보존. L25r3/r4 예정반복 중단. 기존 --spi-opt(동일32MHz, DATA burst 동안 SPIM세션유지+직접SPI 전송)으로 hotpath를 단축한 P25를 추가한다. 새 수신/송신 PHY나 재시도는 추가하지 않는다. 첫P25가 구조적으로 유효하고 모든노드PER<1%이면 동일HEX로 확인3회; 아니면 결과를 검토해다음선택.
