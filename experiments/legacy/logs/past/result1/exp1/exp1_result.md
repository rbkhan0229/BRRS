# 7/1 exp1 실험결과

## 32sym

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=3, 32sym) =====
My TX: success=999 attempts=999 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=0
--- Latency per node ---
===== END STATS =====

```

```
#RX

===== BRRS FINAL STATS (PLEN=3, 32 sym) =====
--- PER per node ---
N2: rx=998 expected=1000 miss=2 PER=0.20% err=0
RX timeouts=2  RX errors=0  delayed late=0
--- Latency per node ---
N2: min=4739us max=4750us avg=4747us (n=998)
--- RX offset from SYNC ---
N2: min=5685us max=5697us avg=5694us (n=998)
--- UWB RX offset from SYNC TX ---
N2: min=3715us max=3715us avg=3715us (n=998)
===== END STATS =====
```

---

## 64sym

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=7, 64sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=0
--- Latency per node ---
===== END STATS =====

```

```
#RX

===== BRRS FINAL STATS (PLEN=7, 64 sym) =====
--- PER per node ---
N2: rx=1000 expected=1000 miss=0 PER=0.00% err=0
RX timeouts=0  RX errors=0  delayed late=0
--- Latency per node ---
N2: min=4778us max=4789us avg=4786us (n=1000)
--- RX offset from SYNC ---
N2: min=5724us max=5736us avg=5733us (n=1000)
--- UWB RX offset from SYNC TX ---
N2: min=3748us max=3748us avg=3748us (n=1000)
===== END STATS =====
```

---

## 128sym

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=15, 128sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=1
--- Latency per node ---
===== END STATS =====

```

```
#RX

===== BRRS FINAL STATS (PLEN=15, 128 sym) =====
--- PER per node ---
N2: rx=1000 expected=1000 miss=0 PER=0.00% err=0
RX timeouts=0  RX errors=0  delayed late=0
--- Latency per node ---
N2: min=4856us max=4869us avg=4866us (n=1000)
--- RX offset from SYNC ---
N2: min=5802us max=5816us avg=5813us (n=1000)
--- UWB RX offset from SYNC TX ---
N2: min=3814us max=3814us avg=3814us (n=1000)
===== END STATS =====
```

---

## 256sym

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=31, 256sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=0
--- Latency per node ---
===== END STATS =====

```

```
#RX

===== BRRS FINAL STATS (PLEN=31, 256 sym) =====
--- PER per node ---
N2: rx=1000 expected=1000 miss=0 PER=0.00% err=0
RX timeouts=0  RX errors=0  delayed late=0
--- Latency per node ---
N2: min=4982us max=4995us avg=4992us (n=1000)
--- RX offset from SYNC ---
N2: min=5932us max=5946us avg=5943us (n=1000)
--- UWB RX offset from SYNC TX ---
N2: min=3946us max=3946us avg=3946us (n=1000)
===== END STATS =====
```