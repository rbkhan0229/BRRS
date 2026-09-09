# Git consolidation validation — 2026-09-09

- Ten original source directories map to preserved Git commits. The seven
  directories without Git metadata were imported as archive branches.
- All 3,759 file versions match their original SHA256 and executable modes.
- All 12,049 indexed local artifact files were present and matched size/SHA256
  at consolidation time. The raw files and firmware binaries remain local.
- 267 lightweight historical reports/helper scripts were copied byte-for-byte
  into experiments/legacy; their original paths/hashes are indexed.
- The 110 bundle records include preparation and TEST_ONLY entries. This count
  is not a count of valid RF experiments and makes no PER claim.
- The current INIT/TX C source and SES project are byte-identical to the latest
  vehicle_beacon512_20260908 copy. Firmware behavior was not changed for this
  consolidation.
- All 61 offline regression tests passed. Fixture dependencies now live in the
  repository instead of the original machine's historical log directories.
- New Git provenance tests distinguish clean commits, tracked/untracked edits,
  and directories outside Git. Case preparation records commit/branch/dirty
  status and preserves the tracked diff; assessment carries this provenance.
- Shell syntax and source patch whitespace checks passed. Historical archive
  and raw fixture whitespace is intentionally preserved through .gitattributes.
  No SSH board connection,
  flash, reset, RF capture, or destructive source/artifact cleanup was run.

GitHub transport and any build-only follow-up are recorded in the local
`DWM3000/logs/git_consolidation_20260909` completion report. Existing main and
historical release references are retained. Current development uses branch
`vehicle-experiments`.
