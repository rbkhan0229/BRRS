# 검증실험

## 32sym_12_0 - 1m office

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

BRRS INIT NODE v1.1 (delayed-RX)
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=229us LEAD=12us TAIL=0us PERIOD=10434us CIR=0
Startup grace: 10000 ms before first SYNC

===== BRRS FINAL STATS (PLEN=3, 32 sym) =====
--- PER per node ---
N2: rx=1000 expected=1000 miss=0 PER=0.00% err=0
RX timeouts=0 (fwto=0 pto=0)  RX errors=0 (sfdto=0 phe=0 fce=0 fsl=0)  delayed late=0
--- Ipatov accumCount (of PLEN) ---
N2: min=14 max=14 avg=14.0 / plen=32 (n=1000)
--- RX-open to RMARKER (expect pre+SFD+lead) ---
N2: min=54us max=54us avg=54us (n=1000)
--- Latency per node ---
N2: min=4745us max=4797us avg=4756us (n=1000)
--- RX offset from SYNC ---
N2: min=5691us max=5744us avg=5703us (n=1000)
--- UWB RX offset from SYNC TX ---
N2: min=3717us max=3717us avg=3717us (n=1000)
===== END STATS =====

```

---

## 32sym_14_0 - 1m office

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=3, 32sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=1
--- Latency per node ---
===== END STATS =====

```

```
#RX

BRRS INIT NODE v1.1 (delayed-RX)
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=231us LEAD=14us TAIL=0us PERIOD=10434us CIR=0
Startup grace: 10000 ms before first SYNC
--- PER per node ---
N2: rx=961 expected=1000 miss=39 PER=3.90% err=39
RX timeouts=0 (fwto=0 pto=0)  RX errors=39 (sfdto=36 phe=0 fce=0 fsl=3)  delayed late=0
--- Ipatov accumCount (of PLEN) ---
N2: min=8 max=16 avg=9.4 / plen=32 (n=961)
--- RX-open to RMARKER (expect pre+SFD+lead) ---
N2: min=56us max=56us avg=56us (n=961)
--- Latency per node ---
N2: min=4745us max=4797us avg=4757us (n=961)
--- RX offset from SYNC ---
N2: min=5691us max=5744us avg=5704us (n=961)
--- UWB RX offset from SYNC TX ---
N2: min=3717us max=3717us avg=3717us (n=961)
===== END STATS =====

```

---

## 32sym_16_0 - 1m office

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

BRRS INIT NODE v1.1 (delayed-RX)
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=233us LEAD=16us TAIL=0us PERIOD=10434us CIR=0
Startup grace: 10000 ms before first SYN

===== BRRS FINAL STATS (PLEN=3, 32 sym) =====
--- PER per node ---
N2: rx=997 expected=1000 miss=3 PER=0.30% err=3
RX timeouts=0 (fwto=0 pto=0)  RX errors=3 (sfdto=0 phe=1 fce=0 fsl=2)  delayed late=0
--- Ipatov accumCount (of PLEN) ---
N2: min=10 max=10 avg=10.0 / plen=32 (n=997)
--- RX-open to RMARKER (expect pre+SFD+lead) ---
N2: min=58us max=58us avg=58us (n=997)
--- Latency per node ---
N2: min=4745us max=4797us avg=4756us (n=997)
--- RX offset from SYNC ---
N2: min=5691us max=5744us avg=5703us (n=997)
--- UWB RX offset from SYNC TX ---
N2: min=3717us max=3717us avg=3717us (n=997)
===== END STATS =====

```

---

## 32sym_18_0 - 1m office

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

BRRS INIT NODE v1.1 (delayed-RX)
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=235us LEAD=18us TAIL=0us PERIOD=10434us CIR=0
Startup grace: 10000 ms before first SY

===== BRRS FINAL STATS (PLEN=3, 32 sym) =====
--- PER per node ---
N2: rx=999 expected=1000 miss=1 PER=0.10% err=1
RX timeouts=0 (fwto=0 pto=0)  RX errors=1 (sfdto=0 phe=0 fce=0 fsl=1)  delayed late=0
--- Ipatov accumCount (of PLEN) ---
N2: min=12 max=12 avg=12.0 / plen=32 (n=999)
--- RX-open to RMARKER (expect pre+SFD+lead) ---
N2: min=60us max=60us avg=60us (n=999)
--- Latency per node ---
N2: min=4745us max=4759us avg=4756us (n=999)
--- RX offset from SYNC ---
N2: min=5691us max=5706us avg=5703us (n=999)
--- UWB RX offset from SYNC TX ---
N2: min=3717us max=3717us avg=3717us (n=999)
===== END STATS =====

```

---

## 32sym_20_0 - 1m office

```
#TX

===== BRRS NODE 2 v1.1 (delayed-TX) FINAL STATS (PLEN=3, 32sym) =====
My TX: success=1000 attempts=1000 delayed_late=0 (PER -> INIT)
SYNC loss: 1 timeouts  RX errors=1
--- Latency per node ---
===== END STATS =====

```

```
#RX

BRRS INIT NODE v1.1 (delayed-RX)
BRRS v1.2: SYNC_PLEN=63 DATA_PLEN=3(32sym) PRE_US=33 SLOT=717us RX_WIN=237us LEAD=20us TAIL=0us PERIOD=10434us CIR=0
Startup grace: 10000 ms before first SYNC

===== BRRS FINAL STATS (PLEN=3, 32 sym) =====
--- PER per node ---
N2: rx=1000 expected=1000 miss=0 PER=0.00% err=0
RX timeouts=0 (fwto=0 pto=0)  RX errors=0 (sfdto=0 phe=0 fce=0 fsl=0)  delayed late=0
--- Ipatov accumCount (of PLEN) ---
N2: min=14 max=14 avg=14.0 / plen=32 (n=1000)
--- RX-open to RMARKER (expect pre+SFD+lead) ---
N2: min=62us max=62us avg=62us (n=1000)
--- Latency per node ---
N2: min=4745us max=4797us avg=4756us (n=1000)
--- RX offset from SYNC ---
N2: min=5691us max=5744us avg=5703us (n=1000)
--- UWB RX offset from SYNC TX ---
N2: min=3717us max=3717us avg=3717us (n=1000)
===== END STATS =====
```