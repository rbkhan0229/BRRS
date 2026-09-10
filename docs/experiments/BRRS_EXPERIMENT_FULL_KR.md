# BRRS Full profile — 999 case

Full은 기존 전수 계획의 새 이름이다. S1~S6, M32/64/128/256, PAC4/8과 예정된
반복을 모두 수행하며, Exp4의 논리 슬롯 배정을 6 blocks마다 한 바퀴 회전해 총 두
주기를 얻는다.

## 전체 구성

| 구분 | 조건과 반복 | case |
|---|---:|---:|
| Stage0 grid | 41 leads × 2 PAC | 82 |
| Stage0 confirmation | 2 PAC × 선정 lead × 5 blocks | 10 |
| Exp1 | 4 M × 2 PAC × 5 blocks | 40 |
| Exp2 | 4 M × 2 PAC × 6 links × 3 blocks | 144 |
| Exp3 | 3 variants × 3 blocks | 9 |
| Exp4 | 58조건 × 12 blocks | 696 |
| Exp5 | 6 links × 3 blocks | 18 |
| **합계** |  | **999** |

## 단계별 조건

- Stage0: 물리 N4→INIT 단일 링크, M32, PAC4/8, lead 0~40 us 전체 grid.
- Exp1: 물리 N4→INIT, M32/64/128/256 × PAC4/8, 조건당 2,000회.
- Exp2: 각 case에서 N2~N7 중 한 물리 TX만 활성화한다. M32/64/128/256 ×
  PAC4/8 × 6링크, 조건당 1,000회와 CIR 요약을 기록한다.
- Exp3: M32/PAC8에서 A=SFD8+표준 PHR, B=SFD16+표준 PHR,
  C=SFD8+data-rate PHR을 비교한다.
- Exp5: N2~N7 단일 링크를 M1024/PAC32로 측정하고 성공 프레임의 raw CIR을
  제한된 수만큼 보존한다.

## Exp4의 58조건/block

S는 실제 활성 TX 수, K는 한 슈퍼프레임의 보고 슬롯 수다. S1~S5는 K=S이고,
S6는 공통 K6과 PHY별 포화 후보를 함께 측정한다.

| 구분 | M과 K | PAC 포함 case/block |
|---|---|---:|
| S1~S5 | 각 S에서 M32/64/128/256, K=S | 40 |
| S6 | M32: K6/12/13 | 6 |
| S6 | M64: K6/12 | 4 |
| S6 | M128: K6/10 | 4 |
| S6 | M256: K6/8 | 4 |
| **합계** |  | **58** |

물리 장착 위치는 바꾸지 않는다. Block 1은 원래 설치 매핑, block 2~6은 논리
노드/슬롯 배정을 한 칸씩 회전한다. Block 7~12는 같은 6회전의 두 번째 주기다.
따라서 각 block은 PHY 조건의 반복인 동시에 슬롯 역할에 대한 block 설계다.

## 끊어 실행하는 round

Stage0 92 case를 끝내 lead를 동결한 뒤 Exp1~Exp5를 round 단위로 실행할 수 있다.

| round | 실행 stage/block | case |
|---:|---|---:|
| 1~3 | Exp1 8 + Exp2 48 + Exp3 3 + Exp4 58 + Exp5 6 | 각 123 |
| 4~5 | Exp1 8 + Exp4 58 | 각 66 |
| 6~12 | Exp4 58 | 각 58 |

Round 1이 끝난 뒤 쉬어도 된다. 새 `full-1` profile은 만들지 않고 각 stage를
`--profile full --blocks 1`로 실행한다. Round 2는 `--blocks 2`를 사용한다.
Full 완료 판정은 선택한 일부 slice가 아니라 999 case 전체가 유효할 때만 가능하다.
실제 RF는 campaign `run --one-case`로 한 case씩 진행한다. 사람이 지나가면 그 case만
오염 표시하고 새 bundle로 대체하므로 나머지 full 순서와 회전 index는 바뀌지 않는다.

## Full을 선택할 때

Full은 실제 TX 수가 S1에서 S6까지 증가하는 과정, M64/M128 중간점, 두 번째
Exp4 회전 주기까지 논문에 필요할 때 사용한다. 차량 시간이 부족하면 먼저
[Essential](BRRS_EXPERIMENT_ESSENTIAL_KR.md)을 확보하고, 장비 점검만 필요하면
[Lite](BRRS_EXPERIMENT_LITE_KR.md)를 사용한다.
