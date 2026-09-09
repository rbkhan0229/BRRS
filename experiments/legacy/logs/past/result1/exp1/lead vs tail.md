# lead vs tail

## 32sym - lead margin only

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=3, 32sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
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
N2: min=4738us max=4750us avg=4747us (n=998)
--- RX offset from SYNC ---
N2: min=5684us max=5697us avg=5694us (n=998)
--- UWB RX offset from SYNC TX ---
N2: min=3715us max=3715us avg=3715us (n=998)
===== END STATS =====
```

---

## 32sym - tail margin only

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
N2: rx=0 expected=1000 miss=1000 PER=100.00% err=0
RX timeouts=1000  RX errors=0  delayed late=0
--- Latency per node ---
--- RX offset from SYNC ---
--- UWB RX offset from SYNC TX ---
===== END STATS =====

```