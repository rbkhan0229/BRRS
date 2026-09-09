# M32/PAC4 SFD timeout 진단 — 새 실행 1회

사용자 요청: 다음 단계 진행, 동일 조건 반복 생략. 기존 결과를 기준으로 사용하고 새로운 후보 R13P4S64를 1,000 superframes만 측정한다.

현재 차량 대비 집 모사 배치, TX 6대, sequence 2345672345673, 13슬롯을 유지한다. M32/PAC4/G250/lead 25us/SB 3000us/SP 2500us/SF 10ms, RX 창 122us/FWTO 119UUS, 슬롯별 delayed-RX, SPI 최적화, 무재전송 및 비수신 구간 정책을 유지한다. INIT=1050270933, TX N2=1050211584/N3=1050273888/N4=1050282818/N5=1050208509/N6=1050227627/N7=1050204212. 외부 전원 허브 및 현재 배치·방향·케이블·포트는 유지한다.

기준은 직전 캠페인의 R13P4_r1: N4 7.15%, SFD timeout 99, PHR error 41, RXFSL 1, FWTO 2. 수신된 N4 패킷의 실제 RMARKER 오차는 248~264ns였다. 성공한 패킷만의 타이밍 관측이므로 실패한 패킷 도착 시각을 보증하지 않는다.

변경은 INIT DATA SFD timeout 37→64심볼이다. M32 프리앰블을 64심볼로 늘리는 변경이 아니다. ELF의 DATA config 16바이트 비교에서 SFD timeout 하위 바이트 하나만 달라졌으며 SYNC config는 같다. TX는 직전 6개 HEX 그대로다. 실제 레지스터 DTUNE0 offset 2를 첫 DATA 설정 직후 한 번만 읽고 결과를 종료 후 출력한다. 슬롯별 처리 경로에 진단 SPI나 로그를 추가하지 않았다.

근거: Qorvo DW3xxx API Guide(2025) p34는 SFD timeout이 잘못된 프리앰블 검출에서 복구하기 위한 장치이며 권장값을 preamble+1+SFD−PAC로 설명한다. 현재 권장식은 37이다. 64는 제조사 권장값을 대체한다는 주장이 아니라, 수신창 내 이른 오검출 뒤 SFD 대기가 너무 빨리 끝나는 가설을 확인하기 위한 한 번의 진단값이다. RX 창 전체 한도를 유지하므로 무제한 수신이나 재수신으로 손실을 감추지 않는다.
https://forum.qorvo.com/uploads/short-url/xD3TlXKvkujjdUaJWXv2b4E7GQN.pdf

기존 PER/노드별 송신량·비컨·슬롯·RDB·SPI·마커·flash readback 검증에 SFD 실제 적용값 확인을 추가한다. 성공 판정은 각 노드 PER <1%, RX 0은 무효다. 과거 실행과 시간 차이가 있어 결과 차이만으로 인과 관계를 확정하지 않는다. NLOS 6.9m 또는 차량과 같은 채널로 간주하지 않는다. 원본 저장소를 변경하지 않고 독립 소스 사본에서만 수정한다. GitHub 업로드는 보류한다.
