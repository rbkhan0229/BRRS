# Preamble 32/64 symbol 비교

| Preamble | RX/1000 | PER | SFDTO | PHE | accumCount 평균 |
|---:|---:|---:|---:|---:|---:|
| 32 symbol | 764 | 23.60% | 225 | 11 | 14.7 / 32 |
| 64 symbol | 994 | 0.60% | 6 | 0 | 47.2 / 64 |

## 핵심 결과

- Preamble을 32에서 64 symbol로 늘리자 PER이 `23.60%`에서 `0.60%`로
  `23.0%p` 감소했다. 상대적인 PER 감소율은 `97.5%`이다.
- 두 조건 모두 TX는 `1000/1000`, delayed-TX late는 0이므로 손실은
  수신 획득 과정에서 발생했다.
- 32-symbol 실패 236건 중 225건(`95.3%`)이 SFD timeout이고 11건이 PHR
  header error였다.
- 64-symbol 실패 6건은 모두 SFD timeout이었다.
- 성공 프레임의 평균 accumCount는 32 symbol에서 `14.7`, 64 symbol에서
  `47.2`였다. PLEN 대비 비율은 각각 `45.9%`, `73.8%`이다.

## 해석 주의

accumCount는 수신에 성공한 프레임에서만 읽은 값이므로 실패 프레임을
포함한 전체 분포가 아니다. 따라서 accumCount는 PER의 직접 원인값이
아니라 성공 프레임의 수신 획득 품질을 나타내는 보조 지표로 해석한다.
