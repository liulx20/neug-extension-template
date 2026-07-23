# Extension tests

| File | What it covers |
|------|----------------|
| `ldbc_test.cc` | Extension `Name()` and `LOAD` by path |
| `wiggle_test.cc` | Demo scalar `wiggle()` |
| `ic2_test.cc` | IC2 function set and argument binding |
| `ic3_test.cc` | IC3 function set |
| `ic_test.cc` | IC1, IC4–IC14 function sets and smoke queries |
| `is_test.cc` | IS1–IS7 function sets |
| `python/test_wiggle.py` | Python `LOAD ldbc` + `wiggle()` smoke test |
| `python/test_ic2_call.py` | Python `CALL ic2` smoke test |

Run C++ tests via `make test` from the repo root.
