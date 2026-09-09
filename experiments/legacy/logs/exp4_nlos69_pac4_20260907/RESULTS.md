# PAC4 단일 시험과 권장값 해석 · 2026-09-07

## 결과와 현재 상태

NLOS6.9m, 유전원 TX 허브. 같은 M32/S3/G250/lead25/SPI 최적화/SB3000/SP2500/1000SF. INIT DATA PAC만8→4로 변경했으며, 이에 따라 SFD timeout은33→37심볼로 자동 계산된다. TX 세 보드는 정확한 기존 HEX 및 비컨 PAC8 설정 그대로다. 보드 배치/방향/포트/케이블/출력/재전송 정책은 유지했다.

| 노드 | offered / TX attempt / TX success | RX | PER |
|---|---|---:|---:|
| N2 | 1000 / 1000 / 1000 | 1000 | 0% |
| N3 | 1000 / 1000 / 1000 | 968 | 3.2% |
| N4 | 1000 / 1000 / 1000 | 1000 | 0% |

전체2968/3000, PER1.067%. 각 TX beacon1000/missed0/late0/END1. 시스템/수집 검증PASS, 각 노드 PER<1% 목표FAIL. 모든 RX 창 attempted=armed1000/late0, RDB mismatch/incomplete/recovered/resync/overrun0, wrong-length/slot/SF0, SPI오류0, 수집timeout0. 유효RX0이 아니며 손실을 숨기지 않았다.

오류는 전부 N3 슬롯에 귀속됐다. PHR14, SFD timeout3, FWTO15, RXFSL/CRC/PTO0. FRAME timeout은 창 안에 수신을 완료하지 못했다는 뜻이며, 15건 전부를 프리앰블 미검출로 단정할 수 없다. PHY 오류와 timeout의 합계32가 N3 손실32와 일치한다.

시험은 사용자 요청대로1회만 실시했다. 시험·readback 구간은11:29:56–11:30:25 KST, RF구간약10초. PAC8 최종4회는11:17:33–11:19:29에 수집됐으므로 같은시각 교차 A/B가 아니다.

INIT1050270933는 검증된 PAC8 성공 HEX로 복원하고 populated flash bytes readback PASS를 확인했다. 추가 RF 실행 없이 reset-halted 상태다. TX는 기존 펌웨어/END 종료 상태다. 사용자 지시에 따라 GitHub 업로드 보류. commit/push/새브랜치/새worktree 생성 없음. 원본 Git/USB 연결 보존 및 잔여 캡처 없음은 postflight_verification.json에 기록했다.

## 왜 PAC4가 권장인데 더 나쁠 수 있는가

1. 제조사 권장은 맞다. DW3000 User Manual v1.1 p39 Table12는32심볼/6.81Mbps에 PAC4를 권장한다. 같은 절은 프리앰블이 충분하다면 더 큰 PAC가 더 좋은 성능을 줄 수 있고, 너무 크면 짧은 프리앰블에 불리하다고 설명한다. PAC는 프리앰블 검출의 상관 처리 단위다. PAC4가 프리앰블 또는 전체 채널 추정 길이를4심볼로 줄인다는 뜻은 아니다.
2. 이 권장은 짧은 프리앰블에서 검출과 이후 축적에 시간을 배분하는 절충으로 이해해야 한다. 특정 NLOS 채널과 수신 진입 타이밍에서 PAC4가 언제나 최소PER을 보장하는 조건은 문서에 없다. 더 긴 PAC8 상관이 약한 링크에서 유리할 가능성과, PAC4가 짧은 M에서 더 많은 후속 처리 여유를 남길 가능성이 함께 존재한다.
3. 이번 로그는 PHY 앞부분의 문제와 일관된다. PAC8 4회는 N3 10/4000손실(0.25%), RXFSL8/PHR2였고, PAC4는1회32/1000손실(3.2%), PHR14/SFD3/FWTO15였다. PHR/SFD 오류가 늘고 N2/N4는정상이며 MAC/예약/RDB 오류0이라는 점에서 N3의 획득/동기화/초기복조 마진 저하가 유력한 가설이다. RXFSL0은 수신 개선을 의미하지 않는다. 더 앞 단계에서 실패하면 데이터 Reed–Solomon 오류 단계에 도달하지 않을 수 있다.
4. 원인은 아직 확정되지 않았다. 낮은 수신전력/다중경로/오검출/잔류 주파수 오차/짧은 창의 영향은 현재 카운터만으로 서로 구분되지 않는다. PAC8에 맞춰 찾은lead25를PAC4에도 그대로 썼으며, PAC4의 창 시작/종료 민감도는 측정하지 않았다. PAC4는 단일실행이므로 약10분 사이 채널 변동도 배제할 수 없다. 제조사 권장이 틀렸거나 PAC8이 M32에서 보편적으로 우월하다는 결론은 내리지 않는다.

## 설정 오류 검토

소스 config_data는 채널9/프리앰블32/PAC숫자override/코드9/8심볼SFD/6.81Mbps/표준PHR/STS OFF를 사용한다. dwt_configure는 DWT_PAC4 enum3을 DTUNE0 PAC bits1:0에 기록한다. 공통 PD_THRESH_OPTIMAL=0xAF5F35CC를 적용한다. 실제 DATA 단계의 무선 레지스터 readback을 추가한 진단 펌웨어는 이번에 실행하지 않았다.

ELF의 config_data 및 config_sync 초기화 바이트를 비교했다. PAC8→PAC4에서 DATA config의 PAC enum0→3과 SFD timeout0x21→0x25만 변경됐고 SYNC config는 동일했다. compiled_config_evidence.json 참조. 따라서 PAC enum4를 잘못 넣었거나 SFD timeout33을 그대로 둔 실수는 소스·바이너리 기준으로 배제했다. SFD timeout37은 제조사 식32+1−4+8과 일치한다. FWTO119UUS, 창122µs, lead25µs는 PAC8과 같다. HEX를 실험 전후 해시/flash readback으로 검증했다.

가장 분별력 있는 다음 검사는 같은 위치·전원·lead25에서 PAC8→PAC4→PAC8을 짧은 간격으로 교차 반복하는 것이다. 차이가 반복되면 수신창 시작/종료를 각각 독립적으로 비교하고, 처리 경로를 교란하지 않는 방식으로 프리앰블 누적량·CFO·오류 시 수신상태를 수집해야 한다. 이번 요청에서는 추가 RF 실험이나 진단 코드 수정을 하지 않았다.

제조사 자료: DW3000 User Manual §4.1.1–4.1.6/p39–40, §8.2.7.1–3/p146–147. 로컬 원문과 관련 페이지 전체를 확인했다.
https://caramelfur.dev/docs/DW3000-User-Manual/DW3000-User-Manual.html

## 0330 식과 현재 이용률

0330-09-ko III-C 식(4)은 L을PSDU바이트로 놓고 Mµs + 8L/6.81µs + 5µs 가드만 분모에 넣는 이상적 점근식이다. SFD/PHR과 비컨·대기 비용을 포함하지 않는다. 현재는 M32, PSDU26B=프로토콜8B+센서16B+FCS2B다. PAC는 공중 프레임 길이를 바꾸지 않아 PAC8/PAC4의 아래 명목 이용률은 같다.

같은 PSDU26B로 비교하면:

- 이상적인 식(4): (208/6.81)/(32+208/6.81+5) = **45.22%**.
- 실제 슬롯만의 점근적 환산: (208/6.81)/347 = **8.80%**.
- 실제 센서16B만 유효 데이터로 세는 슬롯 이용률: (128/6.81)/347 = **5.42%**.
- 현재3개TX/10ms 전체 슈퍼프레임의 센서 데이터 이용률: (3×128/6.81)/10000 = **0.5639%**, offered38.4kbps.
- PAC8 성공4회 손실까지 반영한 goodput38.368kbps/6.81Mbps = **0.5634%**.

347µs는 전송 프레임 예산97µs+가드250µs다. 세부 PHY 모델은 프리앰블32.56416µs+SFD8.14104µs+PHR21.53844µs+PSDU 및RS32.82176µs=95.0654µs이고, 코드가 각 항을 정수µs로 올려33+9+55=97µs를 예약한다. 실제RF파형을 오실로스코프로 측정한 값이 아닌 코드·PHY 시간 모델이다. 논문의8L/Rd 정규화와 코드의RS 포함 송신시간을 동일한 값으로 혼동하지 않았다.

문서의L10/M16 약35.8%는 현재L26/M32와 다른 조합이다. 현재 센서16B를 곧바로L=16으로 넣어33.69%라고 하면 프로토콜8B/FCS2B를 빠뜨린 가상의16B PSDU 계산이 되므로 현재 펌웨어 이용률로 제시하지 않는다. 현재8.80%도 DATA슬롯을 연속해서 채운 점근적 환산이며, 비컨/준비/유휴시간을 포함한 실제3노드 시스템의 달성값은 아니다.

결과적으로 현재 PER<1% 달성은 신뢰성 검증이며 논문의 점근적 효율 달성과 동일하지 않다. 가드250µs와 SFD/PHR 유지 및3TX/10ms 운용의 영향을 각각 구분해야 한다.

## 이미지와 기록

- PAC4시험 init, serial 1050270933, SHA256 `b623dbb1fab13c1807260f552e576fa5c705ca9c76f7f925e6e3af5717f60d0e`
- PAC4시험 N2, serial 1050211584, SHA256 `9b84be4f94f75257b64b52ac248195d54e669315dc3f8cae054e99a41606ab20`
- PAC4시험 N3, serial 1050273888, SHA256 `9a1fa14ef6fcfd81619c33c5720aec2603b6c8ceb3c1403717f4e7d6ed58f651`
- PAC4시험 N4, serial 1050282818, SHA256 `3b4611d82afb69f4be3a23aa9275c20bab6d02a49a77ac7baf8c79c9789c07e1`

복원PAC8 INIT SHA256 `9840bb124de05bd83af7f58b5b494c734443c0ca81ee933c9c168febcfbd2373`.

원본C4_r1 로그/status/audit/manifest/flash readback을 보존했다. 서브타입 원문:

```text
RX timeouts=15 (fwto=15 pto=0)  RX errors=17 (sfdto=3 phe=14 fce=0 fsl=0 fint-only=0 overrun=0)  delayed schedule late=0  data config errors=0
```
