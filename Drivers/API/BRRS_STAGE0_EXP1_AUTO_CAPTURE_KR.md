# Stage0 / Experiment 1 자동 수집

두 실험 모두 `build -> flash -> RTT channel 1 저장 -> 내부 일관성 검증 -> meta 생성`을 한 명령으로 수행한다.

## 준비

```bash
cd Drivers/API
chmod +x brrs_stage0_capture.sh brrs_exp1_capture.sh brrs_exp1_verify.py
python3 -m pip install pylink-square
```

제출용 조건과 반복 전체를 실험별 한 명령으로 실행하려면
`BRRS_SUBMISSION_AUTO_RUN_KR.md`와 `brrs_run_experiment.sh`를 사용한다. 아래
내용은 단일 조건만 직접 실행할 때의 방법이다.

한 컴퓨터에 J-Link가 여러 개 연결되면 각 명령 뒤에 `--serial <S/N>`을 붙인다.

## Stage0: lead margin sweep

TX를 먼저 실행하고 `READY marker seen`을 확인한 뒤 RX를 실행한다. 전체 자동 탐색은
`brrs_run_experiment.sh stage0 ...`로 lead 0~40 us를 1 us 간격으로 수행한다.

```bash
# TX 노트북
./brrs_stage0_capture.sh tx 15 1 iron_door_nlos 6.9

# RX/INIT 노트북
./brrs_stage0_capture.sh rx 15 1 iron_door_nlos 6.9
```

두 번째 반복은 run 번호만 `2`로 바꾼다. Lead 0~40 us의 준비된 구성을 사용할 수 있다. Tail 비교용으로 준비된 구성은 다음 두 개다.

```bash
./brrs_stage0_capture.sh rx 0  1 iron_door_nlos 6.9 --tail 100
./brrs_stage0_capture.sh rx 12 1 iron_door_nlos 6.9 --tail 100
```

## Experiment 1: preamble sweep

Stage0에서 선택한 lead와 tail 0 us 조건으로 32/64/128/256 symbols를 측정한다.
아래 예시는 lead 15 us이며, 다른 값이면 TX와 RX 명령에 같은 `--lead`를 붙인다.

```bash
# 예: 32 symbols, run 1
./brrs_exp1_capture.sh tx 32 1 iron_door_nlos 6.9 --lead 15
./brrs_exp1_capture.sh rx 32 1 iron_door_nlos 6.9 --lead 15
```

같은 방법으로 `32`를 `64`, `128`, `256`으로 바꾸고, 두 번째 반복은 run 번호를 `2`로 바꾼다.

## 판정 의미

- `collection=PASS`: 2,000회가 끝났고 설정, delayed scheduling, 구조화 로그가 서로 일치한다.
- `link=PASS`: 2,000/2,000 성공이다.
- `link=LOSS`: 실제 무선 손실이 있었다. 원시 로그 저장 실패가 아니다.
- `[verify] PASS`: 링크 손실 여부와 무관하게 해당 실험 결과가 분석 가능한 형태로 온전히 저장되었다.
- `end=1`/`end_tx=3`: TX가 현재 실행의 END를 받았고 INIT이 END 비컨 3회를 송신했다.

원시 로그와 메타데이터는 SDK 상위의 `logs/stage0_*` 또는 `logs/exp1_*` 폴더에 저장된다. 같은 run 번호를 의도적으로 다시 쓸 때만 `--force`를 붙인다.
