# Home dryer placement · one PAC8 run

User installed TX nodes inside dryer and RX behind dryer at home. This is a new environment; do not pool it with NLOS6.9m. Execute exactly one1000-SF run using the previously verified P25 M32/PAC8, per-slot bounded scheduled delayed-RX, lead25us, SPI optimization, G250/S3/SB3000/SP2500/10ms SF. Exact historical TX images. No TX/RX role swaps, movement, power/port changes, retransmissions, builds, source edits, commits or pushes. GitHub remains on hold.

Confirm actual probes: localINIT1050270933; remoteN2=1050211584,N3=1050273888,N4=1050282818. Stop only positively identified stale experiment processes if needed. Capture raw RTT and END markers on all boards, source/slot/SF and RDB/window integrity, errors, TX beacons/attempts/success/late, all image hashes and populated-byte flash readback.

Require offered1000 per node, TXsuccess1000 per node, system/collection errors0 and nonzero accepted RX for valid full-TX comparison. Goal is every node PER strictly<1%. Ordinary PHY error/FWTO is packet loss, not hidden. No automatic RF repetition regardless of outcome. Preserve any user-reported contaminated run and exclude it from valid interpretation. Hub power answer may be recorded in a separate environmental note if it arrives after the immutable capture manifest is frozen.
