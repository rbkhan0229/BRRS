# 과거 → 현재 → 과거, 동일 조건 비교 결과

2026-09-04 17:25–17:29 KST, 총3회 완료. 17:31 로컬/원격 수집 프로세스 잔존 없음 확인.

## 조건

사용자 확인 원래 위치, 같은 전원 구성과 고정 역할: INIT1050270933, N2=1050211584, N3=1050204212, N4=1050282818. M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF. IRQ/PHYfast/진단/프로파일링 OFF. 과거는8/25 실제 사용 HEX4종과 hash일치, 현재는55a23fa에서 비교 조건으로 빌드하고 SPI 최적화 ON. 동일 RTT collector, 명시적 serial, 모든 실행 네 보드 각각 재플래시. 역할 회전 없음.

## 결과

| 순서 | 버전 | N2 PER | N3 PER | N4 PER | 전체 RX/3000 | 전체 PER | 판정 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | 과거 old_r1 | 0% | 49.0% | 0% | 2510 | 16.333% | FAIL_PER |
| 2 | 현재 current_r2 | 0% | 49.9% | 0.3% | 2498 | 16.733% | FAIL_PER |
| 3 | 과거 복귀 old_r3 | 0% | 46.9% | 0.6% | 2525 | 15.833% | FAIL_PER |

- 모든 실행 TX3대 모두 비컨1000/1000, 시도/송신 성공1000/1000, END완료. 9개 TX 캡처 총9000회 성공 송신.
- 공통 관측 항목: wrong length/slot/superframe, delayed RX/TX late, RDB mismatch/incomplete/recovered/resync, overrun 모두0.
- 현재만 있는 SPI/timeout/recovery 및 rearm deadline 카운터는0. 과거에 없는 SPI 계측을0으로 보충하지 않는다.
- 모든 역할 raw hash, 펌웨어 hash, serial, run/side 및 설정 검증 완료. 현재와 과거에 맞는 검증기를 각각 사용하고, 공통5% PER 제한은 별도로 유지했다.
- 과거 RX 오류: old_r1 총409 (SFD timeout66, PHR55, CRC6, RXFSL282); old_r3 총387 (SFD timeout40, PHR57, CRC10, RXFSL280).
- 현재 RX 오류416 중415는 fint-only. 진단OFF라 오류 상세 소실이 있어 과거와 subtype 빈도를 직접 비교하지 않는다. fint-only나 RXFSL0을 오류 감소로 해석하지 않는다.

## 해석

과거 정확한 바이너리로 복귀해도 높은 N3 손실이 재현됐다. 현재49.9%와 과거49.0/46.9%는 모두 고손실 구간이다. 이 matched G250/긴wait/4212 조건에서는 '최근 소프트웨어 변경이 큰 손실을 새로 만들었고 과거로 돌아가면 회복된다'는 설명을 지지하지 않는다.

작은 버전 차이는 확정하지 않는다: 현재1회/과거2회, 고정순서이며 이 결과는 비열화/동등성 검정이 아니다. 과거2회 N3합산PER47.95%와 현재49.9%의 차이는1.95%p이지만 반복/시간변동과 분리되지 않았다. 최근 G200/짧은wait/fast 조건 전체를 면책하는 결과도 아니다.

8/25 과거 결과 N3(888)1.8%와 오늘 과거 결과 N3(4212)46.9–49.0%의 차이는 아직 설명되지 않았다. 같은 위치 조건이라는 사용자 확인을 유지하며 '환경이 바뀌었다'고 단정하지 않는다. 보드가888→4212로 다른 비교라는 한계가 남아 있다. 예전에도 포함된 수신 진입 이력과 보드/경로 상호작용 등 공통 요인도 미해결이다.

다음으로 정보량이 높은 검증은 원위치·같은 케이블/포트/역할을 유지하고 당시888에 정확한 과거 N3 바이너리를 적용해 확인하는 것이다. 물리 교체는 사용자 확인이 필요한 별도 단계이며 이번에 수행하지 않았다. 새 코드 수정이나 큰 규모 B/C 실험은 시작하지 않았다.

## 보존과 최종 상태

- 바이너리/원본경로/해시/RTT주소: manifest.json, images/old 및 images/current.
- 실행별 init/tx 원시로그·console·수집상태·audit.json은 old_r1, current_r2, old_r3 디렉터리에 보존.
- current HEX/ELF/build logs 보존. 빌드 SEGGER Embedded Studio8.28, nRF5SDK17.0.2; ELF .comment는 GCC15.2.1(arm-15.2.Rel1) 및 SEGGER C/C++20.1.3(clang20.1.8). SPI32MHz/CPU64MHz, Debug 기본O0/directSPI함수O3. 과거 빌드의 정확한 toolchain/source commit은 미확정이며 동일HEX재사용으로 재빌드 혼입을 피했다.
- 펌웨어 추적 소스 수정 없음. 현재 worktree는 기존 SDK link만 untracked; main/동결 브랜치 변경 또는 push 없음.
- 마지막 플래시 상태는 네 보드 모두 **과거 G250 바이너리**. 최신 펌웨어로 자동 복원하지 않았다.
