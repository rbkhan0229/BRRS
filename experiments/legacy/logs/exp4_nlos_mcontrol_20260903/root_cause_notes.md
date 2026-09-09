# Read-only root-cause review, 2026-09-03

No confirmed firmware defect or defective board has been identified. The code review did not modify firmware or access hardware; the main task performed the radio controls.

## Position, board, and slot are not isolated yet

The position sequence at M32/full configure/SB1703/SP2002 was original → swapped → restored → same-image reflash/restart. N3/888 PER was36.7→5.6→48.3→43.3%; N2/584 was0→4.9→0.1→0.1%. Board identity stayed tied to the logical role, so this is not an isolated board-versus-slot test. The nominal bad position did not give the same PER on both boards, and time/orientation/placement precision remain confounders.

Current Exp4 code uses delayed RX to start the first slot, then immediate manual RX rearm inside a bounded DATA burst, not an independent delayed-RX opening for every subsequent slot. Relevant source: `Drivers/API/Src/examples/ex_35a_brrs_init/brrs_init.c:2404` and `:2968` in the diagnostic SDK. This architecture was also present at the broad lifecycle level in rev24. N2's first slot and N3's rearmed slot have different receiver entry histories. Do not describe the implementation as independently scheduled delayed-RX for every slot based only on its startup banner.

The most discriminating next schedule-only check is 234 → 324 → 234 with physical boards/USB topology unchanged. This gives888/N3 the first slot and584/N2 the second, without changing board identities. Existing --sequence support changes INIT's schedule; sensors already read beacon slot ownership. This is a proposed next test, not performed in this campaign. A loss shift with slot would implicate receive/slot history or slot-dependent preparation; loss staying with888 at the bad position would support a board/path association, not by itself prove hardware defect.

## What recent counters do and do not show

In the SB1703 reflash M32 run, RX-good events=RDB dispatches=buffer releases=2562; mismatch/incomplete/resync/overrun were zero. N3 successfully received packets split284/283 across host buffers. Successful RX timestamps were about288–312ns from scheduled RMARKER; all three transmitters reported1000 TX completions. These observations do not support a simple one-buffer failure, wrong-slot discard, or TX-omission explanation, but they do not prove correct RF reception of missing frames.

An established diagnostic limitation is that the error path rearms RX before reading SYS_STATUS (brrs_init.c:5963–5965), then uses a FINT fallback when details are no longer available (:6021). In the same run344/346 errors were FINT-only, and N3 had433 misses but341 attributed error events. Do not interpret FINT_RXERR as specifically PHR, or CRC count0 as proof that no packets failed at CRC. Subtype information is insufficient to select an RF mechanism.

## Historical lower-PER data is real, but was not a clean system PASS

The Aug25 `logs/exp4_nlos_6.9m_latest_6.9m_g200_l15_pac8_20260825/exp4_32_s3_r1_init.log` records N3 RX978/1000 (2.2%) under M32/PAC8/S3/G200/lead15, PSDU26/app16, owner order234 and slot297us. The archived TX assignments at `experiment_logs_20260825/TX_remote/exp4_nlos_6.9m_latest_6.9m_g200_l15_pac8_20260825/exp4_32_s3_r1_multi_tx.assignments.csv` confirm888=N3,584=N2,818=N4. This is not the separate G250 dataset.

However, the old INIT run reports collection=FAIL: wrong-length1, overrun1, RDB mismatch1, incomplete1 and resync1. The observed low PER is evidence requiring explanation, not a fully qualified system baseline. SB/SP was3000/2500, INIT/TX revision24/24 rather than current47/25, with metadata and error-rearm ordering differences. The broad first-delayed/manual-burst RX lifecycle already existed.

Archived N3 metadata has firmware SHA2563496edeef48a0379241b1afdfb913279290c362577dd5000bffc69f2e0d1e50d, captured2026-08-25T16:48:41+0900; TX reported1000 beacons/1000 successes. Exact M32/S3 INIT metadata and git commit are missing from the available archived run, so do not substitute another mode's INIT hash. The independent review verified duplicate INIT raw logs against the archive's SHA256SUMS: f7c9ca52ea0ffc166af2b8586b2743d0af2b2147ce1cfdaab46dd10f4d315f3f (raw-log hash, NOT firmware hash). r2 also assigns888=N2 with994/1000 RX=0.6%, likewise collectionFAIL.

Therefore an M256 success today would not explain why the same M32 board had lower observed PER previously. Wait budgets, RX implementation, environmental history and power conditions still differ. Do not close the firmware-regression investigation solely on the M256 outcome.
