# 차량 글러브박스 RX · TX6 · 단일 실행

**목표 실패. 측정1회 종료, 보드7대 halt, 잔여 캡처0.**

SSH s-macbook-air와 실제 RX1/TX6 serial 정상. M32/PAC8·lead26µs·G250·SB/SP3000/2500µs·13슬롯·1000SF. 슬롯 순서2345672345673, 슬롯별 bounded delayed-RX·RX창123µs/FWTO120UUS. 재전송 없음. RX=글러브박스, TX=범퍼2·1열2·트렁크2. 시동 켜짐·허브 외부 전원·모든 문 닫힘 사용자 확인. 차종 및 창문·보닛·트렁크 별도 상태는 미제공.

| 노드 | 위치 | Serial | 예정 | TX성공 | RX | 예정 대비 PER | 비컨 |
|---|---|---|---:|---:|---:|---:|---:|
| N2 | 범퍼 A | 1050211584 | 2000 | 0 | 0 | 100.00% | 0/1000 |
| N3 | 범퍼 B | 1050273888 | 3000 | 0 | 0 | 100.00% | 0/1000 |
| N4 | 운전석 | 1050282818 | 2000 | 2000 | 1723 | 13.85% | 1000/1000 |
| N5 | 조수석 | 1050208509 | 2000 | 2000 | 1938 | 3.10% | 1000/1000 |
| N6 | 트렁크 A | 1050227627 | 2000 | 2000 | 2000 | 0.00% | 1000/1000 |
| N7 | 트렁크 B | 1050204212 | 2000 | 2000 | 1965 | 1.75% | 1000/1000 |

전체 예정13000, TX성공8000, RX7626. 예정 대비 손실41.3385%. 송신한8000개 기준 DATA손실374개(4.675%).

N2/N3는 비컨 수신0·TX attempt0을 리셋 없이 종료 후 ELF 심볼에 따른 RAM에서 확인했다. 두 노드100%는 예정 대비 미전달이며 송신 후 PHY PER로 해석하지 않는다. N4~N7은 TX attempt=success=2000, 비컨1000/1000, TX late0, 종료 마커1개씩이다.

RX1000SF 원문은 완전하지만 전체7보드 수집은 INCOMPLETE/FAIL이다. N2는180초 END timeout, N3는 END가 없고 supervisor가 중단했다. N3가 별도 timeout에 도달했다고 기록하지 않는다. 두 노드 원문도 remote_capture_files 아래 보존했다. 정상 수집/PASS로 분류하지 않는다.

RX timeouts=5000 (fwto=5000 pto=0)  RX errors=374 (sfdto=362 phe=10 fce=0 fsl=2 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0

FWTO5000은 범퍼의 예정 슬롯5000개와 일치한다. DATA 오류374개는 SFD timeout362·PHR10·RXFSL2. 모든 슬롯1000회 arm, delayed-RX late0. wrong length/slot/superframe, RDB mismatch/incomplete/resync/overrun, SPI error/timeout, queue overflow는 원문0이다.

7HEX는 어제 NLOS lead26/K13 성공 실행과 논리 역할별 SHA256이 동일하고, 실행 후 실제 flash readback도 모두PASS. 이번은 원래 물리 역할, 과거0% 실행은 회전block2이므로 환경·역할 차이가 있다. 펌웨어C와 원본 Git 변경 없음. 어제의 모든TX 송신 후 DATA 고손실과 달리 이번에는 비컨 초기 연결 실패가 함께 있다. continuous RX 단독 원인으로 설명할 수 없고 현재 이미 슬롯별 delayed-RX다. 다음 우선순위는 범퍼 두 링크의 비컨 획득 실패 확인이다.

제어시각(KST):2026-09-08 11:38:49~11:42:09. RX DATA약10초. 추가 RF실행 없음. 현재lead26 HEX 유지·7대 halt.

## HEX SHA256

| 역할 | SHA256 |
|---|---|
| init | e79340b212fbbb941506ba9c42693e41ba66232789fb3a575b4913cd20830262 |
| N2 | f2b1052a13bdec77110b24f1c8e4c032868804f1ab3ad8dbd90b58362fb5a5fb |
| N3 | e389c39afb4dd58a2b73473dcfdfd721f6c059f4b0e2625e4a94cc5f69a900b8 |
| N4 | 42737cae87424c4a91a52336d509c766e596fd809a085ff5665818a90690b8d2 |
| N5 | fc4ab72282253ed567e8f40881ac0f368fd44701ebd3c19c12d4d299d6d82ae3 |
| N6 | 37d108c1610f649a7645ca8e2c90f7bd36398a6af855ba29d1147daff25a2096 |
| N7 | 55c387c78f0ce76c8ef2afa307f2d60f4e2bb624c5a3c0f86bdcee46533cba6f |

[상세 JSON](RESULTS.json) · [RX 원문](capture1/results/local/init.log) · [범퍼 RAM·TX flash 확인](recovery_remote.json) · [실패 제어 기록](capture1/results/orchestration.json) · [설치 기록](manifest.json)
