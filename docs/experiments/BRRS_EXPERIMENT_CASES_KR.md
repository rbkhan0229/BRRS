# BRRS 차량 실험 profile 안내

이 문서는 `brrs_vehicle_manifest.json`과 현재 자동 실행기의 공통 안내 및 profile
목차다. 정확한 조건과 반복 계획은 아래 세 문서로 분리했다.

- [Full — 999 case](BRRS_EXPERIMENT_FULL_KR.md)
- [Essential — 279 case](BRRS_EXPERIMENT_ESSENTIAL_KR.md)
- [Lite — 133 case](BRRS_EXPERIMENT_LITE_KR.md)

범위와 증거 강도는 **full > essential > lite**다. 과거 `paper` 이름은 기존
manifest와 bundle을 읽기 위한 호환 별칭이며 새 실행에는 사용하지 않는다.

| profile | 조건 범위 | 반복 | confirmation 포함 총 case |
|---|---|---|---:|
| `full` | 기존 전수 조건 | 단계별 3~12 blocks, Exp4 2회전 주기 | **999** |
| `essential` | 논문의 핵심 조건 | 단계별 3~6 blocks, Exp4 1회전 주기 | **279** |
| `lite` | essential과 같은 기본 조건 | 모든 조건 1 block, 고정 배정 | **133** |

## case, block, round

- `case`: PHY, PAC, lead, 활성 TX, 슬롯 수와 배정, 반복 번호가 고정된 한 번의
  무선 실행이다.
- `block`: 한 stage의 비교 조건을 각각 한 번씩 실행한 한 바퀴다.
- `round`: 여러 stage의 같은 block 번호를 차례대로 수행하기 위한 현장 실행
  단위다. 실행기가 저장하는 공식 단위는 case와 block이며, round는 쉬어 갈 지점을
  설명하는 운영 용어다.
- Exp4 block이 바뀌면 물리 보드 위치는 그대로이고 논리 노드/슬롯 배정만 회전한다.
  따라서 완전히 같은 복제가 아니라 보드·위치 효과와 슬롯 역할 효과를 분리하기 위한
  의도된 반복이다.

## Stage0는 round와 분리

Stage0은 뒤 단계에서 사용할 PAC별 lead를 결정하므로 매 round마다 반복하지 않는다.

1. Stage0 0~40 us 전체 grid를 완료한다.
2. PAC4/PAC8 후보를 고른다.
3. 선택한 profile의 confirmation을 완료한다.
4. lead가 동결된 새 manifest로 Exp1~Exp5를 시작한다.

Full/essential은 선정 lead를 PAC별 5회, lite는 PAC별 1회 확인한다. 차량 배치나
환경을 바꾸면 기존 Stage0을 이어 붙이지 않고 새 환경 manifest에서 다시 수행한다.
Exp4 S6/K6의 lead 후보 sweep은 세 profile 모두에 포함되지 않는 선택적 캘리브레이션이다.

## Full을 한 round만 실행하고 쉬기

`full-1`이라는 별도 profile은 만들지 않는다. Full의 block 1만 실행하려면 같은
`--profile full`에 `--blocks 1`을 사용한다. 그러면 case ID와 metadata가 full로
남아 나중에 block 2 이후와 함께 집계할 수 있다.

```bash
python3 brrs_suite_campaign.py prepare --manifest vehicle_frozen.json \
  --stage exp4 --profile full --blocks 1 \
  --root /private/tmp/vehicle_full_round01_exp4 --dry-run
```

`--dry-run`은 계획만 보여준다. 실제 bundle 준비 시 이를 빼고, 준비된 root는 다음처럼
실행한다.

```bash
python3 brrs_suite_campaign.py run \
  --root /private/tmp/vehicle_full_round01_exp4 --one-case
```

`--one-case`는 이미 완료된 case를 검증해 건너뛴 뒤 **다음 미완료 case 하나만** RF로
실행하고 제어권을 돌려준다. 같은 명령을 다시 호출하면 다음 case로 진행한다. 차량
현장에서는 이 옵션을 기본으로 사용하며, 옵션을 빼야만 root에 남은 case가 연속 실행된다.

Exp1~Exp5의 block 1을 각각 별도 root로 준비·실행하면 full round 1이 된다. 다음에는
`--blocks 2`와 새 root를 사용한다. 한 root의 campaign 명세는 생성 후 바꾸지 않는다.
완료 bundle은 다시 실행할 때 원문과 hash를 확인한 뒤 건너뛰며, 여러 root의 bundle을
최종 결과 도구에 함께 전달할 수 있다.

안전하게 쉬는 시점은 **한 case가 끝난 직후 또는 stage/round 경계**다. case 도중 강제
중단하면 해당 수집은 실패 자료로 보존되고 자동 재실행되지 않는다.

## 사람이나 차량이 지나간 경우

- case 사이에 지나가면 다음 `--one-case` 명령을 시작하지 않고 기다린다.
- 약 10초 RF 측정 도중 지나가면 현재 case ID와 교란 시각을 알리고, 가능하면 case가
  정상 종료되도록 둔다.
- 해당 raw와 metadata는 삭제하거나 덮어쓰지 않고 오염 실행으로 표시한다.
- 같은 case는 새 root/bundle에서 나중에 다시 측정하며, 기존 bundle은 이유와 hash가
  적힌 exclusions 기록으로 최종 통계에서 제외한다.
- 사람/차량 통행 때문에 전체 round나 campaign 순서가 꼬이지 않으며 그 case 하나만
  대체하면 된다.

## 어떤 profile로 시작할지

- 전체 장비와 경로를 빠르게 확인하려면 `lite`를 사용한다.
- 논문의 우선 데이터는 `essential`로 확보한다.
- 시간이 충분하고 S1~S5, M64/M128, 두 번째 Exp4 회전 주기까지 필요하면 `full`로
  확장한다.
- 처음부터 full 완성을 목표로 한다면 lite를 먼저 만들지 말고 `full --blocks 1`로
  시작한다. Profile명이 조건 hash에 포함되므로 lite bundle은 full/essential의 첫
  block으로 자동 합산되지 않는다.

## Profile 밖의 탐색

다음 항목은 999/279/133에 포함하지 않는다.

- 차량 장착 직후 6링크 사전 점검;
- 선택적 S6 lead 후보 확인;
- SB, SP, guard, 최대 K 탐색;
- 사람 통행·케이블 접촉·수집 실패로 오염된 case의 대체 측정;
- 비기본 비컨 프리앰블 등 즉흥 탐색.

탐색은 고유 로그 경로에서 저수준 실행기로 수행하고, 채택할 조건만 새 manifest에
동결한다. 전체 준비·실행·재개·집계 방법은
[Stage0~Exp5 실행 경로](../../Drivers/API/BRRS_PAPER_CAMPAIGN_KR.md)를 따른다.
