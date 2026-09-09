# NLOS 6.9m — lead 탐색·회전·전선 보고 후 재측정

PAC8은 현재 회전 배정에서 **25µs 최악0.55%, 26·27µs는 6대 모두0%**였다. PAC4의 이전 최저는27µs/N7 1.00%였으며, 사용자가 전선 가림을 알린 뒤 같은 조건을1회 재측정하자 **N7 6.8%, 나머지0%**였다. 각 TX PER<1% 기준으로 PAC4는 둘 다 실패다. 전선이 가렸던 보드와 시작 시점은 불명이며, 이전 자료를 임의로 제외하거나 새 결과와 합산하지 않았다.

실행 수: Stage0 신규17회+기존1회 재사용, Exp4 20회, 요청한 별도 재측정1회. 마지막 요청에 따라 나머지 탐색은 중단했다. 진행 중이던 PAC8/30µs 캡처는 정상 종료 후 보존했다. 원래 배정/25µs의 추가 RF 반복은 하지 않았다.

## 현재 상태와 연결

로컬 INIT1050270933, SSH `s-macbook-air`, 원격 TX6대 모두 정상. 허브 외부 전원 확인 상태를 유지했다. 로컬·원격 preflight/postflight에 실제 serial·USB 허브 트리·프로세스·Git을 보존했다. 원본 Git4곳의 branch/HEAD/dirty/diff hash는 전후 동일하다. 펌웨어 C 수정·commit·push 없음.

마지막에는 **원래 물리 역할의 PAC8/25µs/S6/13슬롯 HEX**로7대를 복원하고 readback 및 halt를 검증했다. 복원 후 RF는 추가하지 않았다. 잔여 캡처 프로세스 없음.

## 조건과 회전 배정

Exp4: M32, G250, SB/SP3000/2500µs, SF10ms, 1000SF,13슬롯 `2345672345673`, 슬롯별 bounded delayed-RX, 기존 SPI 최적화. S2 대표만2슬롯 `23`. RX lead는 **예상 프리앰블 시작보다 앞서 켜는 추가 여유**다. `RX_EARLY=PREAMBLE+SFD+lead`, 전체 창은 `97+lead`µs이며, 남겨 받는 프리앰블 길이를 뜻하지 않는다. TX6대의 역할별 HEX는 sweep 전체에서 동일하며 RX PAC/lead만 바뀐다.

| 물리 역할 | serial | 이번 block2 논리 역할 | 원래 역할로 복원 |
|---|---|---|---|
| init | 1050270933 | INIT | init |
| N3 | 1050273888 | N2 | N3 |
| N4 | 1050282818 | N3 | N4 |
| N5 | 1050208509 | N4 | N5 |
| N6 | 1050227627 | N5 | N6 |
| N7 | 1050204212 | N6 | N7 |
| N2 | 1050211584 | N7 | N2 |

## Exp4 결과 — 각 물리 노드 PER(%)

| PAC | leadµs | 슬롯/TX | N2 | N3 | N4 | N5 | N6 | N7 | 전체 PER | 판정 |
|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| 8 | 20 | 2/2 | — | 0.000 | 0.000 | — | — | — | 0.0000 | PASS |
| 8 | 20 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 7.800 | 71.400 | 12.1846 | FAIL_PER |
| 4 | 20 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.150 | 64.900 | 10.0077 | FAIL_PER |
| 8 | 24 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.350 | 3.300 | 0.5615 | FAIL_PER |
| 4 | 24 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.050 | 51.850 | 7.9846 | FAIL_PER |
| 8 | 28 | 13/6 | 0.050 | 0.000 | 0.000 | 0.000 | 0.000 | 11.550 | 1.7846 | FAIL_PER |
| 4 | 28 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 48.750 | 7.5000 | FAIL_PER |
| 8 | 32 | 13/6 | 0.000 | 0.000 | 0.033 | 0.000 | 0.100 | 3.100 | 0.5000 | FAIL_PER |
| 4 | 32 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 47.350 | 7.2846 | FAIL_PER |
| 8 | 36 | 13/6 | 0.000 | 0.000 | 0.033 | 0.000 | 0.000 | 11.550 | 1.7846 | FAIL_PER |
| 4 | 36 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 46.750 | 7.1923 | FAIL_PER |
| 8 | 40 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.250 | 15.950 | 2.4923 | FAIL_PER |
| 4 | 40 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 47.700 | 7.3385 | FAIL_PER |
| 8 | 25 | 13/6 | 0.100 | 0.000 | 0.000 | 0.000 | 0.100 | 0.550 | 0.1154 | PASS |
| 4 | 25 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.150 | 56.500 | 8.7154 | FAIL_PER |
| 8 | 26 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | PASS |
| 4 | 26 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 53.650 | 8.2538 | FAIL_PER |
| 4 | 27 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 1.000 | 0.1538 | FAIL_PER |
| 8 | 27 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.0000 | PASS |
| 8 | 30 | 13/6 | 0.000 | 0.000 | 0.000 | 0.000 | 0.950 | 14.250 | 2.3385 | FAIL_PER |

![측정한 lead와 최악 노드 PER](network_lead_per.png)

아까 원래 배정의 PAC8/25µs에서 N6 0.50%, N7 0.70%였던 결과와 모순되지 않는다. 현재 block2에서도 같은25µs로 통과했고, 논리 역할별7개 HEX 모두 아까와 일치한다. 즉 lead를 바꾼 실패를 이전25µs 조건의 재실패로 해석하면 안 된다. 역할 회전과 시간 변동까지 단일 원인으로 배제한 것은 아니다. [HEX 대조](original_vs_rotated_25_hex_comparison.json).

## 수집·오류·해시

모든 완료 실행은 정확한 probe 집합, TX READY→RX, 종료 마커·metadata·raw hash, 실행 후 flash readback을 검증했다. S2의 비참여4TX와 Stage0의 비참여5TX 정지를 전후 확인했다. 수집 PASS와 PER PASS를 분리했고 수신0인 Stage0는 실패 전이 자료로 보존했다.

| PAC/lead | 시작–종료 KST | RX timeout·오류 원문 |
|---|---|---|
| 8/20 S2 | 2026-09-07T23:49:04+09:00–2026-09-07T23:49:51+09:00 | RX timeouts=0 (fwto=0 pto=0)  RX errors=0 (sfdto=0 phe=0 fce=0 fsl=0 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/20 S6 | 2026-09-07T23:50:20+09:00–2026-09-07T23:51:29+09:00 | RX timeouts=1544 (fwto=1544 pto=0)  RX errors=40 (sfdto=2 phe=0 fce=2 fsl=36 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/20 S6 | 2026-09-07T23:51:56+09:00–2026-09-07T23:52:44+09:00 | RX timeouts=1289 (fwto=1289 pto=0)  RX errors=12 (sfdto=0 phe=1 fce=0 fsl=11 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/24 S6 | 2026-09-07T23:54:08+09:00–2026-09-07T23:54:57+09:00 | RX timeouts=1 (fwto=1 pto=0)  RX errors=72 (sfdto=0 phe=0 fce=1 fsl=71 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/24 S6 | 2026-09-07T23:55:27+09:00–2026-09-07T23:56:15+09:00 | RX timeouts=1026 (fwto=1026 pto=0)  RX errors=12 (sfdto=0 phe=0 fce=3 fsl=9 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/28 S6 | 2026-09-07T23:57:27+09:00–2026-09-07T23:58:14+09:00 | RX timeouts=206 (fwto=206 pto=0)  RX errors=26 (sfdto=0 phe=0 fce=1 fsl=25 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/28 S6 | 2026-09-07T23:58:41+09:00–2026-09-07T23:59:29+09:00 | RX timeouts=972 (fwto=972 pto=0)  RX errors=3 (sfdto=0 phe=0 fce=0 fsl=3 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/32 S6 | 2026-09-07T23:59:55+09:00–2026-09-08T00:00:43+09:00 | RX timeouts=0 (fwto=0 pto=0)  RX errors=65 (sfdto=0 phe=1 fce=4 fsl=60 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/32 S6 | 2026-09-08T00:01:10+09:00–2026-09-08T00:01:57+09:00 | RX timeouts=940 (fwto=940 pto=0)  RX errors=7 (sfdto=1 phe=0 fce=0 fsl=6 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/36 S6 | 2026-09-08T00:02:24+09:00–2026-09-08T00:03:12+09:00 | RX timeouts=189 (fwto=189 pto=0)  RX errors=43 (sfdto=1 phe=0 fce=1 fsl=41 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/36 S6 | 2026-09-08T00:03:41+09:00–2026-09-08T00:04:29+09:00 | RX timeouts=925 (fwto=925 pto=0)  RX errors=10 (sfdto=0 phe=0 fce=0 fsl=10 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/40 S6 | 2026-09-08T00:04:56+09:00–2026-09-08T00:05:44+09:00 | RX timeouts=6 (fwto=6 pto=0)  RX errors=318 (sfdto=1 phe=8 fce=12 fsl=297 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/40 S6 | 2026-09-08T00:06:13+09:00–2026-09-08T00:07:01+09:00 | RX timeouts=929 (fwto=929 pto=0)  RX errors=25 (sfdto=0 phe=1 fce=1 fsl=23 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/25 S6 | 2026-09-08T00:07:44+09:00–2026-09-08T00:08:31+09:00 | RX timeouts=2 (fwto=2 pto=0)  RX errors=13 (sfdto=0 phe=0 fce=2 fsl=11 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/25 S6 | 2026-09-08T00:09:39+09:00–2026-09-08T00:10:28+09:00 | RX timeouts=1037 (fwto=1037 pto=0)  RX errors=96 (sfdto=10 phe=82 fce=1 fsl=3 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/26 S6 | 2026-09-08T00:10:54+09:00–2026-09-08T00:11:42+09:00 | RX timeouts=0 (fwto=0 pto=0)  RX errors=0 (sfdto=0 phe=0 fce=0 fsl=0 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/26 S6 | 2026-09-08T00:12:09+09:00–2026-09-08T00:12:58+09:00 | RX timeouts=983 (fwto=983 pto=0)  RX errors=90 (sfdto=0 phe=16 fce=3 fsl=71 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 4/27 S6 | 2026-09-08T00:13:26+09:00–2026-09-08T00:14:14+09:00 | RX timeouts=17 (fwto=17 pto=0)  RX errors=3 (sfdto=0 phe=0 fce=0 fsl=3 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/27 S6 | 2026-09-08T00:14:46+09:00–2026-09-08T00:15:34+09:00 | RX timeouts=0 (fwto=0 pto=0)  RX errors=0 (sfdto=0 phe=0 fce=0 fsl=0 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |
| 8/30 S6 | 2026-09-08T00:16:00+09:00–2026-09-08T00:16:48+09:00 | RX timeouts=96 (fwto=96 pto=0)  RX errors=208 (sfdto=6 phe=69 fce=4 fsl=129 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0 |

TX별 offered/RX/PER, beacon/missed, attempts/success/late/END와 slot별 수신, RXFSL/PHR/CRC/SFD/FWTO/PTO, TDMA 및 RDB mismatch/incomplete/resync/overrun, SPI/수집 상태 원문은 [RESULTS.json](RESULTS.json)에 전부 포함했다. [실제 serial·역할별 HEX/ELF SHA256](image_inventory.json).

## Stage0와 선정 한계

Stage0는 물리N4→INIT 단일 링크, M32, G500, tail0,2000SF였다. 단일 링크에서 고른20µs를6TX에 그대로 적용했을 때 실패한 것이 이번 중요한 발견이다. Stage0의 좋은 값을 전체 네트워크 최적값으로 취급하지 않는다. Stage0 PAC8/25의 직전 자료는 해시와 설치 context를 확인해 재사용했다.

| leadµs | PAC4 PER% | PAC8 PER% |
|---:|---:|---:|
| 0 | 100.00 | 100.00 |
| 5 | 0.00 | 미측정 |
| 10 | 0.05 | 38.65 |
| 15 | 미측정 | 0.05 |
| 19 | 0.00 | 0.05 |
| 20 | 0.00 | 0.00 |
| 21 | 0.00 | 0.00 |
| 25 | 0.00 | 0.00 (재사용) |
| 30 | 0.00 | 0.00 |
| 40 | 0.05 | 0.00 |

PAC8/26µs는 주변25/26/27µs의 최악 노드 PER이0.55/0/0%인 관측 후보다. 전선 보고 후 현재 상태에서 PAC8을 다시 검증한 것은 아니다. PAC4는27µs의1.00%와 재측정6.8% 모두 strict <1%에 미달한다. 추가 정수 lead29/31 등은 마지막 사용자 요청으로 실행하지 않았다. 전역 최적점·논문용 반복 동결·모든 회전·차량 성능 완료를 주장하지 않는다. [후보 근거](refined_network_candidates.json).

현재 자료는 lead와 수신 시작 시점에 민감한 저손실 구간을 지지한다. continuous RX와의 A/B가 아니므로 과거 manual-rearm 구조나 금속·전선 하나를 단일 원인으로 확정하지 않는다. RXFSL 중심 오류와 FWTO 중심 오류가 조건별로 다르며, 현재 측정은 이전 S3/lead15 burst 펌웨어와도 다르다.

PAC4/25 Stage0 준비 중의 첫8-worker 빌드 실패는 RF 전에 멈췄다. 로그를 보존하고 소스 수정 없이1-worker build-only로 복구한 뒤 RF1회만 수행했다. [빌드 기록](build_recovery.json).

[전선 보고 후 별도 재측정 보고서](../exp4_pac4_lead27_cable_recheck_20260908_0018/RESULTS.md)
