# PAC4 one-run check

User requested PAC4 once, then GitHub upload if acceptable. INIT DATA PAC4 only; matching SFD timeout derives from M+1-PAC+SFD. TXs remain exact historical images, including their original beacon RX settings. M32/S3/G250/lead25/SPIopt/SB3000/SP2500/1000SF, powered-hub NLOS6.9m, placement/ports unchanged.

Acceptability: each TX offered1000/attempt1000/success1000; each node PER strictly<1%; schedule/RDB/collection errors0; valid RX nonzero; explicit READY/END and matching flash readback. One run cannot establish PAC4 superiority over PAC8. If it fails, preserve data and restore the known PAC8 image; do not push under the conditional authorization.

GitHub source work will use a separate branch/worktree and include a reproducible preset and measured result documentation. Existing working copies remain unchanged.
