# Exp4 slot-capacity limit after wait/guard evaluation (2026-09-02)

## Outcome

Keep `BRRS_MAX_DATA_SLOTS = 32`. With the smallest validated configuration,
timing permits only **21** DATA slots, so the 32-slot beacon protocol is not the
active limit and increasing it would add wire and RAM cost without capacity.

## Selected-capacity calculation

The Task 4 budgets leave `10000 - 1703 - 2002 = 6295 us` from the first DATA
RMARKER to the configuration-switch deadline. For M32:

- preamble + SFD = 42 us;
- PHR + 26-byte PSDU = 55 us;
- complete frame airtime = 97 us;
- validated guard = 200 us;
- slot interval = 297 us.

The firmware formula is:

`1 + floor((DATA_budget - PHR_PSDU - guard) / slot_interval)`

Thus `1 + floor((6295 - 55 - 200) / 297) = 21` slots. The effective maximum is
`min(21, 32) = 21`.

## Why the cap is not increased

| Guard case | Validation | Timing maximum | Protocol cap | Effective maximum |
|---:|:---:|---:|---:|---:|
| 200 us | PASS 10/10 | 21 | 32 | 21 |
| 100 us | FAIL 7/10 | 32 | 32 | 32, not claimable |
| 75 us | not run | 36 | 32 | hypothetical 32 |
| 50 us | not run | 43 | 32 | hypothetical 32 |

The first mathematically cap-limited point is G=75, but it was intentionally not
run after G=100 produced RDB faults. Therefore there is no measured basis for a
larger beacon schedule in the present implementation.

For future work, raising the cap from 32 to 36 would increase packed owner bytes
from 16 to 18 and beacon PSDU from 39 to 41 bytes. It remains within one 330-bit
RS data block and increases modeled M256 beacon airtime from 337 to 339 us.
Raising it to 43 requires 22 owner bytes and a 45-byte beacon; 360 payload bits
cross the RS-block boundary, producing two parity blocks and a 350 us modeled
beacon. The 36-slot cap is therefore the preferable conditional extension if a
future safe G<=75 implementation is established. It must not be enabled now.

No constant or wire format was changed for Task 6.
