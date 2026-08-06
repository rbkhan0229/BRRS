# Experiment 3 core result

## 핵심 결과

- A/B/C 모두 TX success와 EXTTXE capture가 `1000/1000`, invalid capture는
  0이었다.
- SFD 8-symbol 증가분은 실측 `8.148 us`, 이론 `8.141 us`로 오차율
  `0.086%`였다.
- 표준 PHR과 DTA PHR의 차이는 실측 `18.731 us`, 이론 `18.846 us`로
  오차율 `-0.610%`였다.
- DTA PHR 이론시간 `2.693 us`를 더해 역산한 SFD 8 + 표준 PHR 시간은
  실측 추정 `29.572 us`, 이론 `29.681 us`로 차이는 `-0.109 us`
  (`-0.37%`)였다.
- PER은 A `0.40%`, B `0.00%`, C `0.10%`였다. 실패 5건은 모두 PHR
  header error였고, preamble/SFD timeout은 없었다.

## 해석 범위

이 결과는 SFD 길이와 PHR 전송률 변화에 따른 airtime 차분이 해석적
모델과 일치함을 뒷받침한다. PHR 완전 제거는 직접 측정한 것이 아니며,
`29.572 us`는 DTA PHR 이론시간을 이용한 역산값이다. PER 차이는 단일
실행 결과이므로 조건 간 성능 우열로 단정하지 않는다.
