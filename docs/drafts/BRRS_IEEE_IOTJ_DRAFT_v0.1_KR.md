# 소형 센서 보고를 위한 비컨 참조형 단축 프리앰블 HRP-UWB TDMA: SHR 축소 서브슬롯을 향한 DWM3000 연구

**익명 저자**  
**IEEE Internet of Things Journal 투고용 한글 검토 원고 — 초안 v0.1, 2026년 8월 13일**

**초록—** 고속 펄스 반복 주파수 초광대역(HRP-UWB)은 정밀하고 안전한 거리 측정에 널리 사용되지만, 동일한 무선 장치는 주기적인 센서 데이터도 전달할 수 있다. 그러나 짧은 센서 보고에서는 동기화 헤더와 물리 헤더가 매 전송에서 큰 비중을 차지한다. 본 논문은 코디네이터가 공통 스케줄을 방송하고 센서 노드가 축소된 동기화 오버헤드로 결정론적 슬롯에서 전송하는 비컨 참조형 축소-SHR 서브슬롯(BRRS)을 연구한다. 상용 DWM3000은 프리앰블 검출, 시작 프레임 구분자(SFD), 물리 헤더(PHR)를 제거하는 모드를 제공하지 않는다. 따라서 본 연구에서는 SFD와 PHR을 유지하되 데이터 프리앰블을 256심볼에서 32심볼로 줄이고, 비컨 스케줄 기반 delayed-TX와 delayed-RX를 사용하는 표준 호환 프로토타입을 구현하였다. 철문으로 차폐된 6.9 m 비가시선(NLOS) 링크에서 수신기 lead margin을 15 us로 설정했을 때 32심볼 데이터 프리앰블은 10,000회 전송에서 패킷 손실이 없었다. 회로 진단값은 유효 누산 심볼 수가 M-23 형태로 나타났으며, first-path SNR 증가는 10log10(M-23) 추세와 기준점 보정 후 최대 0.51 dB 이내에서 일치하였다. EXTTXE 차분 airtime 측정으로 현재 유지되는 8심볼 SFD와 표준 속도 PHR의 합산 오버헤드를 29.795 us로 추정하였다. 구현상 guard를 200 us로 둘 때 프리앰블 단축은 프레임 airtime을 325 us에서 97 us로, 슬롯 길이를 525 us에서 297 us로 줄였고, 현재 10 ms 스케줄의 계산상 수용량을 9슬롯에서 15슬롯으로 증가시켰다. 2노드 시험의 합산 패킷 오류율은 32심볼과 256심볼에서 각각 1.30%와 1.175%였으며, 슬롯 또는 슈퍼프레임 위반은 없었다. 이 결과는 단축 프리앰블 기반 스케줄 UWB의 실현 가능성과 타이밍 예산을 보여준다. 다만 이상적 BRRS의 전체 용량 이득을 주장하려면 포화 부하 네트워크 실험과 차량 내 채널 실험이 추가로 필요하다.

**색인어—** 초광대역, IEEE 802.15.4z, DWM3000, 센서 네트워크, TDMA, 프리앰블, delayed reception, 채널 이용률.

> **작업 초안 상태.** 본 원고는 현재 확보한 실측 결과와 향후 검증 항목을 의도적으로 구분한다. Stage 0은 최종 시험 공간 정리 전에 수집되어 재측정할 예정이다. 실험 4는 현재 1개와 2개 송신 노드까지만 포함하며, 더 많은 슬롯과 포화 부하 실험은 아직 수행하지 않았다. 0330 기획 문서의 실험 5인 차량 채널 실험도 예정되어 있다. 최종 투고 데이터는 하나의 고정된 펌웨어 버전에서 원시 로그와 manifest를 함께 수집한다.

## I. 서론

HRP-UWB 무선 장치는 주로 비행시간(Time-of-Flight) 기반 거리 측정, 보안 출입 및 위치 추정에 사용된다. 그러나 IEEE 802.15.4 UWB 물리 계층은 패킷 데이터 전송도 지원하므로, 이미 설치된 UWB 무선 장치를 주기적인 소형 센서 보고에 재사용할 수 있다. 이러한 가능성은 차량 차체 네트워크, 로봇 플랫폼 및 분산 산업 센서처럼 UWB 타이밍 또는 ranging 인프라가 이미 필요한 시스템에서 특히 의미가 있다. 이 경우 UWB를 재사용하면 별도 무선 장치의 추가를 피하고 2.4 GHz Bluetooth Low Energy(BLE) 및 Wi-Fi와 다른 주파수 자원을 활용할 수 있다. 다만 본 연구의 동기는 UWB가 항상 BLE를 대체한다는 것이 아니다. 통신만을 목적으로 하는 소형 payload에서는 전용 BLE 링크가 airtime 측면에서 더 효율적일 수 있다. 본 연구의 질문은 이미 존재하는 UWB 링크의 고정 오버헤드를 충분히 줄여 스케줄 기반 센서 통신을 실용화할 수 있는가이다.

핵심 문제는 유효 데이터에 비해 물리 계층의 고정 오버헤드가 크다는 점이다. HRP-UWB 프레임은 프리앰블과 SFD로 구성된 동기화 헤더(SHR), PHR, 그리고 물리 계층 서비스 데이터 단위(PSDU)로 구성된다. 짧은 센서 보고에서는 프리앰블 시간이 유효 payload 시간보다 수배 길 수 있다. PSDU 전송률을 높여도 이 고정 비용은 사라지지 않는다. 특히 각 노드가 자신의 슬롯에서 짧은 보고 하나를 전송하는 결정론적 uplink에서는 모든 보고가 프리앰블, SFD 및 PHR 비용을 반복해서 지불한다.

0330 기획 문서는 공통 비컨 이후의 중복 획득 오버헤드를 제거하는 비컨 참조형 서브슬롯 구조인 BRRS를 제안하였다. 이상적인 BRRS는 수신기가 blind preamble search를 피하고, 비컨이 알린 스케줄로 데이터 경계를 추론하며, SFD와 PHR을 생략하고, 미세 동기화와 채널 추정에 필요한 프리앰블 심볼만 남길 수 있다고 가정한다. 하지만 DWM3000 응용 프로그래밍 인터페이스에서는 이러한 이상적 수신기를 구현할 수 없다. 따라서 본 연구는 BRRS를 설계 목표로 두되, 그 구성 요소를 표준 호환 프로토타입으로 평가한다. 데이터 프레임에는 프리앰블, SFD 및 PHR이 그대로 존재하며, 비컨은 데이터 프리앰블 길이, PSDU 길이, 전송률, 참여 노드 bitmap, 슬롯 소유자, 첫 슬롯 offset, 슬롯 간격 및 슈퍼프레임 주기를 전달한다. 코디네이터는 예정된 도착 시각 주변에서 수신기를 켜고, 센서는 delayed transmission을 사용한다.

이 구분은 본 논문의 주장 범위를 정하는 데 중요하다. 프리앰블을 256심볼에서 32심볼로 줄이면 프리앰블 심볼 수는 8분의 1이 되지만 전체 슬롯이 8분의 1이 되는 것은 아니다. 현재 구현에서는 200 us guard와 유지된 SFD/PHR 때문에 슬롯이 525 us에서 297 us로 줄어든다. 이는 43.4% 감소이며, 10 ms 슈퍼프레임에서 계산상 데이터 슬롯 수가 9개에서 15개로 증가하는 것에 해당한다. 따라서 완전한 BRRS 이득은 상용 수신기에서 이미 시연된 기능이 아니라 실측한 구성 요소 비용을 이용한 해석적 전망이다.

본 작업 초안의 기여는 다음과 같다.

- 비컨 참조형 소형 보고 TDMA를 구체적으로 설계하고, 이상적 축소-SHR 수신기와 DWM3000 호환 구현의 경계를 명확히 정의한다.
- delayed reception에 명시적인 lead margin이 필요함을 보이고, 시험한 설정에서 수신 획득이 대략 고정된 수의 프리앰블 심볼을 소비한다는 예비 진단 근거를 제시한다.
- 철문으로 차폐된 6.9 m NLOS 링크에서 32–256심볼 프리앰블의 패킷 오류율과 CIR 기반 first-path SNR을 측정한다.
- 0330 계획의 소프트웨어 timestamp 방식을 하드웨어 EXTTXE pulse capture와 SFD/PHR 차분 측정으로 대체하여 고정 pipeline 지연을 상쇄한다.
- 구체적인 비컨 protocol과 다중 슬롯 슈퍼프레임을 구현하고, 프레임 시간, 구현 슬롯 시간, 스케줄 정확성 및 현재 가용 슬롯 수를 정량화한다.

이후 논문은 고정 오버헤드 문제, 제안 설계, 구현 및 실험 방법, 결과, 그리고 저널 투고를 위해 남은 검증 순서로 구성된다.

## II. 배경, 관련 연구 및 문제 정의

### A. HRP-UWB 프레임 오버헤드

데이터 프리앰블 심볼 수를 M, PSDU 길이를 L byte라고 하면 표준 호환 프레임 airtime은 다음과 같다.

$$T_frame(M,L) = M T_psym + T_SFD + T_PHR + T_PSDU(L,C_RS),$$

여기서 T_psym은 프리앰블 심볼 시간이고 C_RS는 물리 계층 block 경계에 적용되는 Reed–Solomon 부호 오버헤드이다. 구현된 슬롯 길이는 다음과 같다.

$$T_slot^impl(M,L) = ceil_us(T_frame(M,L)) + T_guard^impl.$$

본 프로토타입의 유효 application payload는 16 byte이다. 무선 구간의 PSDU는 BRRS protocol header 8 byte, application payload 16 byte, IEEE 802.15.4 frame check sequence 2 byte를 합친 26 byte이다. 따라서 application report를 더 줄이거나 PSDU 전송률을 높여도 프리앰블, SFD, PHR, protocol header 및 guard 비용은 제거되지 않는다.

0330 분석은 근본적인 오버헤드를 드러내기 위해 단순화된 field 시간과 이상적인 5–10 us guard를 사용하였다. 현재 구현은 Reed–Solomon 부호화를 포함한 DWM3000 airtime model과 실측 service budget을 사용한다. 이 model은 32심볼과 256심볼 프레임을 각각 97 us와 325 us로 예측하며, 실험 4에서는 예정된 도착 시각 사이에 200 us guard를 둔다.

### B. 수신 획득과 delayed reception

DWM3000 수신기는 프리앰블 상관, 타이밍 획득, SFD 검출, header decoding 및 payload decoding을 수행한다. 스케줄 기반 delayed reception은 무선 장치가 청취해야 하는 시간을 줄이지만 이러한 획득 기능을 끄지는 않는다. 또한 delayed-RX 명령 시각에 analog 및 digital receive chain이 즉시 유효해진다는 뜻도 아니다. 수신 창은 예상 preamble/SFD 경계보다 프리앰블 시간과 구현 lead margin만큼 먼저 열려야 한다.

데이터 수신기의 opening interval은 다음과 같이 정의한다.

$$T_early = T_preamble + T_SFD + T_lead,$$

$$T_window = T_early + T_PHR+PSDU + T_tail.$$

여기서 T_lead는 수신기 기동, 이산적인 PAC 정렬, clock 및 scheduling 양자화, model에 포함되지 않은 구현 지연을 보상한다. T_tail은 예정된 경계 이후의 수신 가능 시간만 늘린다. 수신기가 초반 프리앰블 심볼을 이미 놓쳤다면 tail을 늘려도 그 심볼을 복원할 수 없다. 따라서 lead-only margin이 tail-only margin보다 수신 성능을 더 크게 개선할 수 있다.

현재 packet acquisition chunk는 PAC8이다. 기존 IEEE 802.15.4z hardware 연구에서도 PAC와 다른 PHY 설정이 수신 감도에 영향을 주는 것으로 보고되었다 [5]. PARIS 역시 차량 내 polling network에서 수신기 활성 시각을 바꾸지만, 목적은 중첩 간섭을 억제하고 frame priority를 적용하는 것이다 [6]. 본 연구는 비컨 기준시각을 사용하여 센서별 프리앰블을 얼마나 줄일 수 있는지와 그 축소가 스케줄된 데이터 수용량을 어떻게 바꾸는지 정량화한다. 따라서 lead-margin sweep 자체를 독립적인 novelty로 주장하지 않으며, 획득 진단, field overhead 측정 및 다중 슬롯 소형 보고 schedule의 통합을 기여로 제시한다.

### C. 문제 정의

하나의 코디네이터와 N개의 센서를 고려한다. 각 10 ms 슈퍼프레임 시작 시 코디네이터는 비컨을 전송한다. 각 활성 센서는 할당된 데이터 슬롯에서 16 byte 보고 하나를 보낸다. 측정된 데이터 경로에는 ACK와 재전송이 없다. 설계 목표는 다음과 같다.

1. 목표 패킷 오류율을 만족하면서 데이터 슬롯 airtime을 최소화한다.
2. 각 데이터 frame에 중복 slot offset을 넣지 않고도 결정론적인 slot identity와 superframe identity를 유지한다.
3. 물리 link loss와 scheduler failure를 구분한다.
4. 수정되지 않은 DWM3000 PHY에서 이상적 BRRS 절감량 중 어느 정도를 실현할 수 있는지 정량화한다.

이를 위해 네 가지 질문을 평가한다. 첫째, 고정된 delayed-RX lead에서 신뢰성을 유지하는 최소 프리앰블 길이는 무엇인가? 둘째, 프리앰블 길이에 따라 first-path SNR은 어떻게 변하는가? 셋째, 이상적 BRRS 수신기가 제거할 SFD와 PHR airtime 비용은 얼마인가? 넷째, 구현된 슈퍼프레임에 표준 호환 단축 프리앰블 데이터 슬롯이 몇 개 들어가며, 다수 노드가 schedule violation 없이 이를 사용할 수 있는가?

## III. BRRS 설계와 DWM3000 호환 구현

### A. 이상적 BRRS와 구현 프로토타입

표 I은 원래 설계 목표와 현재 확보한 근거를 분리한다. 이상적 BRRS frame은 mini-preamble 뒤에 알려진 길이의 data block이 바로 이어진다. DWM3000 프로토타입은 지원되는 프리앰블, SFD 및 PHR을 유지해야 한다. 실험 1과 2는 단축 프리앰블을 직접 시험한다. 실험 3은 유지된 delimiter와 header 비용을 측정하여 이를 해석적으로 뺄 수 있게 한다. 실험 4는 완전한 표준 호환 schedule을 측정한다.

**표 I. 이상적 BRRS와 DWM3000 호환 구현의 주장 경계**

| 구성 요소 | 이상적 BRRS 목표 | DWM3000 프로토타입 | 본 초안의 근거 |
|---|---|---|---|
| 비컨 | schedule 및 PHY profile 전달 | 256심볼 비컨, protocol v3 | 구현 완료 |
| 데이터 프리앰블 | 초기 16–32심볼 mini-preamble | 지원되는 32–256심볼 | Exp1, Exp2 |
| blind preamble search | 기준 타이밍으로 회피 | 획득 engine은 계속 동작 | Stage 0 진단 |
| SFD | 제거 | 8 또는 16심볼 유지 | Exp3 차분 측정 |
| PHR | 길이와 rate를 알려 제거 | STD 또는 DTA rate로 유지 | Exp3 차분 측정 |
| Guard | 해석적 5–10 us | 현재 다중 노드 구현은 200 us | Exp4 |
| 네트워크 규모 | 0330 계획의 물리 노드 4–8개 | 물리 노드 1–2개 측정 | Exp4; 확장 예정 |

이 경계는 표준 32심볼 프리앰블과 프리앰블 검출을 제거한 수신기를 동일시하는 오류를 피한다. 현재 프로토타입은 BRRS를 향한 근거이지 비표준 PHY의 완전한 구현은 아니다.

### B. 비컨 protocol과 슬롯 식별

Protocol-v3 비컨의 PSDU는 39 byte이며 공통 source, destination, message type, protocol version 및 16-bit superframe sequence를 포함한다. 스케줄 field는 데이터 프리앰블 길이, 데이터 PSDU 길이, 전송률, active-node bitmap, 첫 슬롯 RMARKER offset, 슬롯 간격, superframe period, slot count 및 압축된 slot-owner 목록이다. 최대 32개 데이터 슬롯을 전달할 수 있다. 7-bit active bitmap은 N2–N8 센서 identity를 나타내며, 명시적인 owner list를 사용하면 반복되거나 비연속적인 할당도 가능하다.

데이터 frame은 동일한 8-byte 공통 header와 16-bit superframe sequence를 포함하지만 slot offset을 반복하지 않는다. 코디네이터는 물리 수신 RMARKER와 비컨 schedule을 이용하여 가장 가까운 예정 RMARKER를 선택함으로써 슬롯을 식별한다. 잘못된 길이, superframe, slot 및 configuration은 각각 별도로 거부한다. 즉 누가 언제 전송하는지는 데이터 payload가 아니라 비컨이 정의한다.

### C. 슈퍼프레임 타이밍

그림 1은 현재 NLOS 배치와 구현 타이밍을 보여준다. 코디네이터 비컨은 256심볼 프리앰블을 사용한다. 첫 데이터 RMARKER는 비컨 TX RMARKER로부터 3000 us 뒤에 예약된다. 실험 4에서 코디네이터는 10 ms 슈퍼프레임의 마지막 2500 us를 data-to-beacon PHY 재설정과 delayed-beacon 준비에 예약한다. 따라서 데이터 슬롯은 3000–7500 us의 4500 us 구간을 사용한다.

![그림 1. 철문 NLOS 실험 배치와 구현된 비컨 스케줄 슈퍼프레임.](assets/fig1_system_and_superframe.png)

슬롯 간격을 T_slot, decoding된 data duration을 T_DP라고 하면 현재 static capacity check는 다음과 같다.

$$N_max = 1 + floor((T_budget - T_DP - T_guard) / T_slot),$$

결과는 32-slot 비컨 format을 넘지 않도록 제한된다. 이는 이상적 BRRS utilization formula가 아니라 측정된 guard와 코디네이터 service time을 포함한 구현 수용량이다.

### D. 수신 타이밍 calibration

Stage 0은 M=32, T_tail=0을 고정하고 T_lead를 sweep한다. 예비 결과는 급격한 transition을 보였다. lead 0–12 us에서는 거의 모든 frame이 실패했고, lead 13 us에서는 acquisition diagnostic이 발생했지만 완전한 frame은 수신되지 않았으며, 14–15 us에서 low-PER 구간으로 진입하였다. lead 13 us의 실패 frame은 diagnostic accumulation count 6 이후 PHR error가 지배적이었고, lead 15 us 성공 frame의 accumulation count는 9였다. 더 큰 lead에서는 관측 accumulation count가 단조 증가하지 않고 PAC 관련 단계로 변했다.

이 결과는 안정적인 높은 accumulation mode와 PER 목표를 동시에 만족하는 가장 작은 lead를 찾고 소량의 robustness margin을 더하는 calibration rule을 뒷받침한다. 그러나 아직 보편적인 상수를 확립하지는 않는다. Stage 0은 통제 공간에서 의자를 제거하기 전에 수행했으므로 재측정해야 한다. 현재 Exp1–Exp4 데이터에는 lead 15 us를 고정했지만 이는 provisional 값으로 표시한다.

## IV. 구현 및 실험 방법

### A. 하드웨어와 PHY 설정

Testbed는 nRF52840 development board가 제어하는 DWM3000 module로 구성된다. Data PHY는 UWB channel 9, preamble code 9, 64-MHz pulse repetition frequency, PAC8, STS off 및 6.81 Mb/s payload rate를 사용한다. 실험 3 variant에서 별도로 변경하지 않는 한 SFD는 8심볼, PHR은 standard rate이다. 비컨 프리앰블은 256심볼로 고정된다. 데이터 프리앰블 M은 {32, 64, 128, 256}이다. 코디네이터는 첫 예정 arrival에 delayed RX를 사용하고 이후 다중 노드 슬롯에는 double-buffer-aware burst reception을 사용한다. 센서는 delayed TX를 사용하고 비컨이 전달한 superframe으로 다음 beacon reception을 예약한다.

Application payload는 16 byte이며 PSDU는 26 byte이다. ACK와 데이터 재전송은 사용하지 않는다. 따라서 보고된 PER은 재전송 이후의 delivery metric이 아니라 처음 schedule된 frame 중 decoding되지 않은 비율이다.

### B. 실험 환경

현재 투고용 데이터 block은 닫힌 철문으로 분리된 6.9 m NLOS 배치를 사용한다. Sensor는 문 기준면 바깥쪽 2.7 m, coordinator는 Room 519 안쪽 4.2 m에 배치하였다. 두 antenna 높이는 바닥에서 약 1.5 m이다. 한 block 안에서 node 위치를 고정하고 run 사이에 preamble 순서를 바꾸었다. 최종 protocol에는 door state, antenna orientation, 주변 물체, 온도, firmware hash, board identity, run order 및 operator intervention을 기록한다.

### C. Metric과 수집 무결성

PER은 100(N_expected-N_rx)/N_expected로 계산한다. 관측 오류가 0인 경우에도 0이 실제 오류 확률 0을 뜻하지 않으므로 one-sided 95% exact upper bound를 함께 제시한다. Scheduler correctness는 wrong-slot, wrong-superframe, delayed-schedule-late, configuration-error 및 collection-status counter로 독립적으로 평가한다.

실험 2는 성공적으로 decoding된 각 frame 이후 DWM3000 CIR memory를 읽는다. First-path SNR ratio는 first-path index 주변 peak power와 도착 전 noise floor로 계산한다. Firmware의 dwt_calculate_rssi 출력은 일부 short-preamble run에서 -128 dBm으로 invalid하므로 본 논문에서는 사용하지 않는다. CIR statistic은 성공 수신 frame에 조건부이므로, loss frame에는 유효한 CIR sample이 없고 PER이 높을 때 survivor bias가 발생한다.

자동 capture는 READY marker, 실험별 completion marker 및 내부적으로 일관된 count를 요구한다. Metadata에는 binary SHA-256, raw-log SHA-256, role, environment, distance 및 collection method를 기록한다. 현재 초안에는 legacy PDF 형식의 Exp1과 Exp4-S2 기록이 일부 포함되어 있으며, 최종 투고에서는 하나의 frozen firmware package에서 수집한 raw log로 교체한다.

### D. 실험 matrix

**표 II. 현재 확보 데이터와 향후 실험**

| 단계 | 변수 | 현재 반복 | 주요 출력 | 초안 상태 |
|---|---|---:|---|---|
| Stage 0 | lead 0–40 us, M=32 | 1–2 x 2000 | PER, failure class, accumCount | 예비 결과; 정리 후 재측정 |
| Exp1 | M=32,64,128,256 | 5 x 2000 | PER, accumCount | 현재 가장 강한 link 결과 |
| Exp2 | M=32,64,128,256 | 2 x 1000 | CIR first-path SNR | 한 환경 완료 |
| Exp3 | A=SFD8/STD, B=SFD16/STD, C=SFD8/DTA | 2 x 1000 | EXTTXE airtime | C run 1은 999 capture; 반복 필요 |
| Exp4 | M=32,256; S=1,2; guard=200 us | 2 x 1000 superframe | slot timing, PER, goodput | scale 및 saturation 예정 |
| Exp5 | 차량 위치와 channel metric | 0 | delay spread, K-factor, path loss | 예정 |

## V. 결과

### A. Exp1: 단축 프리앰블 패킷 수신

T_lead=15 us와 T_tail=0에서 32심볼 데이터 프리앰블은 5회 run, 총 10,000개 schedule frame을 모두 전달하였다. 64심볼은 10,000개 중 1개를 잃었으며, 128 및 256심볼에서는 다시 관측 손실이 없었다. 네 설정을 합친 PER은 40,000개 중 1개 손실인 0.0025%이다. 손실이 없었던 10,000-frame 조건에서 underlying PER의 one-sided 95% exact upper bound는 약 0.030%이다.

**표 III. 실험 1 패킷 수신 및 누산 결과**

| M (심볼) | 수신 / 예정 | 관측 PER | 최빈 accumCount | one-sided 95% PER upper bound |
|---:|---:|---:|---:|---:|
| 32 | 10000 / 10000 | 0.000% | 9 | 0.030% |
| 64 | 9999 / 10000 | 0.010% | 41 | 0.047% |
| 128 | 10000 / 10000 | 0.000% | 105 | 0.030% |
| 256 | 10000 / 10000 | 0.000% | 233 | 0.030% |

성공 frame의 diagnostic mode는 정확히 accumCount=M-23을 따른다. 이는 고정한 타이밍과 PAC8 설정에서 DWM3000이 보고하는 coherent accumulation interval 이전에 약 23개의 프리앰블 심볼에 해당하는 획득 구간이 소비됨을 시사한다. 첫 23개의 물리 심볼이 보편적으로 버려진다는 뜻은 아니며 제조사 독립적인 algorithm을 증명하는 것도 아니다. 이는 다른 환경과 PAC4에서 검증해야 할 device/configuration 종속 경험적 관계이다.

그림 2(a)의 핵심은 현재 sample size에서 네 PER을 통계적으로 구분하기 어렵다는 점이다. 따라서 올바른 해석은 32심볼이 256심볼보다 우수하다는 것이 아니라, 수신 lead를 calibration한 특정 통제 NLOS block에서 32심볼의 reliability penalty가 관측되지 않았다는 것이다.

### B. Exp2: 프리앰블 길이에 따른 CIR 품질

CIR 기반 first-path SNR은 M=32에서 16.71 dB, M=256에서 30.33 dB로 증가하였다. M=32 대비 측정 gain은 64, 128, 256심볼에서 각각 7.05, 10.61, 13.62 dB이다. 전체 길이를 사용하는 10log10(M) 추세는 3.01, 6.02, 9.03 dB만 예측한다. 반면 M=32를 기준으로 한 10log10(M-23) 추세는 6.58, 10.67, 14.13 dB를 예측하며 최대 absolute residual은 0.51 dB이다.

![그림 2. 프리앰블 길이에 따른 Exp1 패킷 오류율과 Exp2 first-path SNR. 유효 model은 현재 PAC8 timing에 대한 경험적 적합이며 보편 법칙이 아니다.](assets/fig2_exp1_exp2.png)

**표 IV. 실험 2 CIR 기반 first-path SNR**

| M | 유효 CIR sample | 평균 FP-SNR | M=32 대비 gain | 기준 보정 10log10(M-23) |
|---:|---:|---:|---:|---:|
| 32 | 1998 | 16.71 dB | 0.00 dB | 0.00 dB |
| 64 | 2000 | 23.76 dB | 7.05 dB | 6.58 dB |
| 128 | 1999 | 27.32 dB | 10.61 dB | 10.67 dB |
| 256 | 2000 | 30.33 dB | 13.62 dB | 14.13 dB |

이 결과는 0330 문서의 processing-gain 주장을 구체화한다. 절대 theoretical coding gain인 10log10(127M)을 측정 first-path SNR과 동일시해서는 안 된다. 현재 실험이 지지하는 것은 겉보기 acquisition budget을 고려한 이후의 증가 추세이다. M-23을 일반 model로 제시하려면 환경 간 반복 검증이 필요하다.

### C. Exp3: 차분 airtime을 이용한 SFD 및 PHR 오버헤드

0330 원안은 RMARKER와 software-observed receive-complete event를 사용하는 절차를 제안하였다. 이 방식은 RMARKER가 SFD 경계에 있고 receive-complete flag에 알려지지 않은 pipeline 및 polling delay가 포함되므로 SHR 또는 data-field의 절대 시간을 얻을 수 없다. 실제 구현은 DWM3000 EXTTXE signal을 nRF52840 input으로 연결하고 GPIOTE/PPI와 16-MHz timer를 이용해 pulse 양쪽 edge를 capture한다. 그 결과 CPU가 edge 사이에 개입하지 않는 62.5 ns timer resolution의 hardware-captured TX airtime을 얻는다.

세 variant가 field cost를 분리한다. A는 SFD8과 standard-rate PHR, B는 SFD만 16심볼로 변경하며, C는 SFD8로 돌아가 PHR만 더 빠른 DTA rate로 변경한다. 두 run의 평균 airtime은 A, B, C에서 각각 95.694, 103.902, 76.801 us였다.

![그림 3. 실험 3 EXTTXE airtime. Variant 차분은 공통 프리앰블, PSDU, 부호화, antenna 및 고정 capture 지연을 상쇄한다.](assets/fig3_exp3_airtime.png)

측정 B-A 차이는 8.209 us이며, 추가된 SFD 8심볼의 해석값 8.141 us와 비교하면 0.83% 차이이다. A-C 차이는 18.893 us이며 STD 및 DTA PHR rate의 model 차이 18.846 us와 비교하면 0.25% 차이이다. Model의 DTA PHR 시간 2.693 us를 더하면 standard PHR은 21.586 us로 추정된다. 따라서 제거 가능한 SFD8 및 standard-PHR 비용은 다음과 같다.

$$T_SFD8 + T_PHR,STD = 8.209 + 21.586 = 29.795 us.$$

절대 측정 airtime은 model보다 약 0.58–0.69 us 크지만 이 공통항은 차분에서 대부분 상쇄된다. 따라서 차분 실험은 원래의 절대 RX timestamp 방식보다 overhead estimate를 더 강하게 뒷받침한다. C variant의 한 run은 1000개가 아닌 999개 pulse를 capture했으며 본 작업 초안에만 유지하고 재측정할 예정이다.

### D. Exp4: 구현 슬롯 길이와 예비 다중 노드 동작

26-byte PSDU에서 측정/model 검증된 on-air frame 시간은 M=32에서 97 us, M=256에서 325 us이다. 200 us guard를 적용하면 slot은 각각 297 us와 525 us가 된다. 따라서 현재 프로토타입은 frame airtime을 70.2%, 전체 slot을 43.4% 줄인다. 4500 us data-slot budget에서 compile-time schedule은 M=32일 때 15슬롯, M=256일 때 9슬롯을 허용하며 이는 66.7% 증가이다.

![그림 4. 실험 4 구현 슬롯 시간과 현재 static slot capacity. 더 높은 수용량은 고정된 schedule의 계산값이며 S=3–15 포화 실험은 예정되어 있다.](assets/fig4_exp4_capacity.png)

1노드 run의 합산 PER은 M=32에서 0.05%, M=256에서 0%였다. 2노드 run은 M=32에서 3948/4000 수신(1.30% PER), M=256에서 3953/4000 수신(1.175% PER)을 보였다. 두 조건의 0.125 percentage point 차이는 작고 node placement와 혼재되어 reliability 순위를 주장할 수 없다. 더 중요한 결과는 guard-200의 모든 run에서 wrong-slot, wrong-superframe 및 delayed scheduling event가 0이었고 schedule, timing, collection이 모두 PASS였다는 것이다.

**표 V. 실험 4 예비 1노드 및 2노드 결과**

| M | 시험 슬롯/노드 | 수신 / 예정 | 합산 PER | 평균 application goodput | 계산상 최대 슬롯 |
|---:|---:|---:|---:|---:|---:|
| 32 | S1 | 1999 / 2000 | 0.050% | 12.794 kb/s | 15 |
| 256 | S1 | 2000 / 2000 | 0.000% | 12.800 kb/s | 9 |
| 32 | S2 | 3948 / 4000 | 1.300% | 25.267 kb/s | 15 |
| 256 | S2 | 3953 / 4000 | 1.175% | 25.299 kb/s | 9 |

두 S2 goodput이 거의 같은 것은 두 조건 모두 10 ms 슈퍼프레임마다 16-byte 보고 2개만 offered load로 주었기 때문이다. 이 run은 light load에서 timing correctness를 검증하지만 predicted capacity gain을 시연하지는 않는다. 결정적인 실험은 offered slot 수를 늘려 M=256이 9-slot limit에 도달한 뒤에도 M=32가 schedule failure나 허용 불가능한 PER 없이 15슬롯까지 계속 동작하는지 확인하는 것이다.

현재 firmware에서는 원래 5–10 us guard를 주장할 수 없음도 확인되었다. Guard-100의 2노드 trial은 collection/schedule acceptance를 만족하지 못했으며 instrumentation은 약 192–193 us의 required service guard를 추정하였다. 따라서 200 us를 frozen implementation setting으로 사용한다. 이 값을 줄이려면 논문의 산술값만 바꾸는 것이 아니라 architecture optimization과 새로운 service-time measurement가 필요하다.

## VI. 논의 및 의의

### A. 비교적 환경 독립적인 결과

세 관측은 구현에 직접 연결되며 software 및 hardware configuration을 고정하면 재현 가능성이 높다. 첫째, 프리앰블을 256심볼에서 32심볼로 줄이면 표준 호환 data frame마다 약 228 us를 제거한다. 둘째, EXTTXE 차분 측정은 유지된 SFD8과 standard PHR의 비용 약 29.8 us를 분리한다. 셋째, 200 us guard와 4500 us data budget은 수학적으로 15 대 9개의 schedule slot을 만든다. 이 결과는 propagation-dependent claim이 아니라 timing과 protocol에 관한 사실이다.

측정된 UWB slot error는 수백 ns 수준인 반면 slot은 수백 us이다. 허용된 Exp4 run에서는 wrong-slot과 wrong-superframe이 발생하지 않았다. 이는 현재 slot spacing에서 beacon schedule과 delayed-TX/RX가 충분히 정밀함을 의미한다. 그러나 mobility, oscillator aging 또는 더 긴 beacon interval에서도 같은 margin이 유지됨을 뜻하지는 않는다.

### B. 환경 또는 장치에 종속되는 결과

최소 신뢰 M, 최적 lead margin, PER, first-path SNR 및 accumulation 분포는 propagation, interference, antenna orientation, receiver implementation, PAC 및 board identity에 의존한다. 현재 iron-door NLOS 결과는 M=32가 동작할 수 있음을 보여주지만 모든 차량 또는 모든 NLOS 공간에서 안전함을 증명하지 않는다. S2 log에서도 node asymmetry가 나타났다. M=32 trial에서는 N3가 대부분의 loss를 보였고 M=256 trial에서는 N2가 대부분의 loss를 보였다. Hardware와 location effect를 분리하려면 node/slot/position swap이 필요하다.

M-23 유효 누산 model은 successful-frame diagnostic mode와 first-path-SNR slope를 함께 설명한다는 점에서 유망하다. 그러나 LOS, iron-door NLOS, vehicle cabin/trunk, PAC4/PAC8 및 board-swap block에서 유지되기 전까지는 가설로 제시해야 한다. 고정 receiver configuration에서 offset이 안정적이고 channel condition에 따라 필요한 M만 바뀐다면 adaptive rule의 기반이 될 수 있다. 최근 CIR quality로 required effective accumulation을 추정하고 목표 margin을 만족하는 가장 작은 M을 비컨으로 알리는 방식이다.

### C. 소형 보고 UWB 네트워크에서의 의미

실용적인 결과는 0330 기획의 이상적 3–9배 utilization보다는 작지만 여전히 의미가 있다. 현재 DWM3000에서는 프리앰블만 줄여도 현재 guard와 service reservation 조건에서 schedule slot capacity가 1.67배 증가한다. 측정한 SFD/PHR 비용은 미래 silicon 또는 standard support로 추가 절감할 수 있는 약 29.8 us를 나타낸다. 이 decomposition은 모든 overhead를 프리앰블 탓으로 돌리지 않고 남은 airtime의 위치를 보여준다.

본 연구는 timing, access 또는 ranging을 위해 UWB radio와 periodic beacon이 이미 존재하는 환경에 가장 적합하다. 아직 통신 전용 센서에서 UWB가 BLE보다 우수함을 증명하지 않았다. 저널 최종본에는 BLE와의 energy/airtime baseline 및 가능하면 차량용 wired 또는 narrowband sensor alternative가 필요하다. IEEE P802.15.4ab의 범위에는 reduced airtime, infrastructure synchronization, peer-to-multi-peer 및 streaming이 포함되어 있어 [7], 현재 prototype이 802.15.4ab 구현은 아니더라도 측정된 overhead decomposition은 시의성이 있다.

### D. 타당성 위협

현재 근거에는 다섯 가지 중요한 제한이 있다. 첫째, Stage 0은 최종 환경 정리 전에 수집되었다. 둘째, Exp1–Exp3가 완성된 controlled propagation geometry는 현재 하나뿐이다. 셋째, 실험 4 schedule은 아직 2노드를 넘겨 부하를 주지 않아 15 대 9 capacity가 해석값이다. 넷째, 현재 근거는 Exp1의 firmware v2.6과 이후 실험의 v2.7에 걸쳐 있으므로 최종 제출에서는 하나의 frozen revision 또는 측정 경로의 bit-equivalent behavior를 입증해야 한다. 다섯째, Exp3 C run 하나는 999개 hardware capture만 포함하며 legacy Exp1/Exp4-S2 기록은 raw machine-readable log가 아닌 PDF text로 보존되어 있다.

실험 2의 측정 제한도 있다. CIR 값은 성공적으로 decoding된 frame에서만 존재하므로 high-PER 조건에서는 first-path-SNR 분포가 낙관적으로 편향된다. 현재 Exp2 PER은 최대 0.1%라 이 block에서는 bias가 작지만, 더 열악한 환경에서는 분석 pipeline이 sample count와 PER을 함께 보고해야 한다.

## VII. 추가 검증 및 향후 연구

다음 실험 단계는 선택적인 완성도가 아니라 주요 validity gap을 닫기 위해 필요하다.

1. **최종 배치에서 Stage 0을 반복한다.** Lead 순서를 randomize하고 12–20 us transition 주변에서 최소 5회 반복하며, failed-frame diagnostic을 유지하고 Exp1 재측정 전에 selection rule을 고정한다.
2. **다양한 환경에서 Exp1과 Exp2를 반복한다.** 최소 LOS, 통제된 철문 NLOS 및 대표 차량 위치를 사용한다. M 순서를 randomize하고 board/position을 swap한다. PAC8을 BRRS baseline으로 유지하면서 short preamble의 standard/vendor 권장 비교값인 PAC4도 시험한다.
3. **Exp3를 완성한다.** Variant C를 반복하고 PSDU-length sweep을 추가하며 Reed–Solomon block boundary를 포함해 airtime slope를 fitting한다. 차분 SFD/PHR 결과는 송신기에서 측정하므로 wireless channel과 무관하게 유지되어야 한다.
4. **Exp4를 포화 상태까지 수행한다.** 두 M 조건에서 계산 limit까지 S=3 이상을 측정하고 limit를 의도적으로 넘겨 fail-closed behavior를 확인한다. 가능한 경우 physical node를 사용하며 repeated-owner schedule은 통제된 emulation으로 명시한다. Aggregate goodput, all-slots-success probability, node별 PER 및 schedule violation을 보고한다.
5. **0330 실험 5의 차량 채널 연구를 수행한다.** Bumper, door, cabin, roof, trunk 및 battery-area 위치에서 CIR을 측정한다. Delay spread, K-factor, path loss 및 channel stability를 추출하고 adaptive M rule이 필요로 하는 minimum effective accumulation과 연결한다.
6. **System baseline을 추가한다.** Fixed-M=256 TDMA, immediate/continuous RX 및 BLE short-report airtime/energy baseline과 비교한다. Energy를 주장하려면 window duration에서 간접 추론하지 말고 radio-state current를 직접 측정한다.

장기적인 목표는 비컨 timing을 직접 사용하여 redundant SFD/PHR을 생략할 수 있는 receiver 또는 standard mode이다. 이러한 hardware가 등장하기 전에도 현재 prototype은 compatibility layer이자 추가 PHY support의 system-level 가치를 판단하는 measurement platform으로 사용할 수 있다.

## VIII. 결론

본 논문은 HRP-UWB short-report overhead를 측정 가능한 cross-layer 문제로 재구성한다. 구체적인 비컨 protocol이 data PHY와 slot schedule을 제공하고, delayed transmission과 reception이 이를 결정론적인 RMARKER 정렬 arrival로 바꾼다. 이어서 preamble, SFD/PHR 및 service guard 비용을 각각 평가한다. 철문으로 차폐된 6.9 m NLOS block에서 calibration된 15 us lead를 적용하면 32심볼 데이터 프리앰블은 Exp1의 10,000 frame을 모두 전달하였다. CIR diagnostic은 gain trend가 설정된 전체 preamble보다는 실제 유효 누산 심볼 수로 더 잘 설명됨을 보여준다. Hardware EXTTXE capture는 SFD8과 standard PHR의 합산 overhead를 29.795 us로 추정하였다. 마지막으로 표준 호환 32심볼 frame은 구현 slot을 525 us에서 297 us로 줄이고 현재 계산상 capacity를 9슬롯에서 15슬롯으로 늘렸으며, S1/S2 trial에서 schedule violation은 관측되지 않았다.

이 결과는 단축 프리앰블 비컨 스케줄 UWB가 BRRS를 향한 실용적인 중간 단계임을 지지한다. 그러나 아직 이상적인 전체 utilization, 보편적인 lead margin 또는 차량 전체의 reliability를 확립하지는 못했다. 최종 논문의 주장은 통제된 반복 검증, saturation-scale Exp4 및 Exp5의 차량 채널 특성화에 달려 있다.

## 감사의 글

저자들은 수신 타이밍, 통제 실험 및 DWM3000 진단값 해석에 관해 지도해 주신 [지도교수와 연구실 구성원 추후 기입]에게 감사드린다.

## 참고문헌

[1] IEEE Standard for Low-Rate Wireless Networks, IEEE Std 802.15.4-2020, Jul. 2020.

[2] IEEE Standard for Low-Rate Wireless Networks—Amendment 1: Enhanced Ultra Wideband (UWB) Physical Layers (PHYs) and Associated Ranging Techniques, IEEE Std 802.15.4z-2020, Aug. 2020.

[3] Qorvo, “DW3000 Family User Manual,” Version 1.1, 2021.

[4] Qorvo, “DWM3000 Module Datasheet,” Rev. B, May 2021.

[5] M. Stocker, H. Brunner, M. Schuh, C. A. Boano, and K. Römer, “On the performance of IEEE 802.15.4z-compliant ultra-wideband devices,” in Proc. CPS-IoTBench, 2022, pp. 28–33, doi: 10.1109/CPS-IoTBench56135.2022.00012.

[6] M. Okuhara, N. Kurioka, S. Mitoh, P. Finnerty, and C. Ohta, “Preamble arbitration rule and interference suppression-based polling medium access control for in-vehicle ultra-wideband networks,” IEEE Open J. Veh. Technol., vol. 5, pp. 1518–1531, 2024, doi: 10.1109/OJVT.2024.3474430.

[7] IEEE 802.15 Working Group, “Task Group 4ab: UWB Next Generation,” 2026. [Online]. Available: https://www.ieee802.org/15/pub/TG4ab.html

[8] S. C. Ergen and A. Sangiovanni-Vincentelli, “Intravehicular energy-harvesting wireless networks: Reducing costs and emissions,” IEEE Veh. Technol. Mag., vol. 12, no. 4, pp. 77–85, Dec. 2017.

[9] M. Okuhara et al., “Optimization of polling-based MAC schedule considering data aggregation for in-vehicle UWB wireless networks,” in Proc. IEEE World Forum on Internet of Things, 2022, doi: 10.1109/WF-IoT54382.2022.10152148.

[10] Bluetooth SIG, Bluetooth Core Specification, Version 5.4, 2023.

[11] IEEE Standard for Information Technology—Telecommunications and Information Exchange Between Systems—Local and Metropolitan Area Networks—Specific Requirements—Part 11, IEEE Std 802.11-2024, 2024.

## 부록 A. 초안 주장-근거 대응표

**표 VI. 주요 주장별 현재 근거와 남은 검증**

| 주장 | 현재 근거 | 추가로 필요한 검증 |
|---|---|---|
| M=32는 사용 가능하다 | 철문 NLOS Exp1에서 0/10000 loss | 다른 환경, board swap, frozen revision |
| 획득은 유효한 고정 budget을 소비한다 | accumCount=M-23; FP-SNR slope 적합 | Stage0 반복; PAC4/PAC8; 차량 channel |
| SFD8 + STD PHR 비용은 약 29.8 us이다 | EXTTXE A/B/C 차분 측정 | C 반복; PSDU sweep |
| 현재 32심볼 slot은 15 대 9슬롯을 지원한다 | 고정 timing model과 S1/S2 schedule correctness | S3–S15 saturation run |
| 완전한 BRRS는 SFD/PHR을 제거할 수 있다 | 해석적 목표만 존재 | receiver/standard 지원 또는 custom PHY |
| 차량 sensor network에 이점이 있다 | 동기 use case | Exp5 차량 channel 및 system baseline |
