# Historical 888 board and exact old-firmware replay

2026-09-04. User replaced N3 with1050273888 at original position and then explicitly limited this campaign to TWO runs. The manifest's initial preparation note says3 repetitions; this user correction supersedes that planning note. Only old_r1 and old_r2 are authorized. Do not run a third repetition or current-firmware comparison automatically.

Fixed roles INIT1050270933(local), N2=1050211584, N3=1050273888, N4=1050282818(remote). M32/PAC8/S3/G250/lead15/SB3000/SP2500/1000SF, exact historical Aug25 four HEX images, all firmware diagnostic/IRQ/fast/persistent-SPI optimizations absent. Same original positions, orientation, cable and powered-hub ports assumed under agreed user procedure; only N3 board replacement is newly confirmed. Board identity now matches historical roles.

All images are hash checked against the prior exact-hash manifest, same bounded collector, serial-specific flashes, no erase-all or overwrites. Old raw verifier plus common cross-role integrity checks and5% diagnostic PER threshold. Old firmware has no modern SPI fault diagnostics; absence must not be reported as measured zero.

Status: exactly TWO runs completed, both FAIL_PER with recorded system integrity checks passed. See [RESULTS.md](RESULTS.md). Local/remote collectors stopped, no third run or current firmware comparison performed. All four boards remain on the historical G250 images; N3 is888.
