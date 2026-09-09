# Offline regression fixtures

The text fixtures retain original packet/counter/CIR rows from the historical
captures listed in origins.json. They are small inputs for parser and identity
regressions, not new RF evidence. Tests fabricate control/readback/assignment
metadata around them; changing a test's assigned physical role does not change
which board produced the historical raw capture.

single_host_exp4_case.json supplies layout only to fake-worker tests.
payload_hashes.json is deliberately empty for an early wrong-index rejection
test; it is not a deployable firmware bundle. No HEX images are included.
