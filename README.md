# LDBC SNB Interactive Extension for NeuG

Out-of-tree NeuG extension that implements LDBC SNB Interactive **read queries** as stored procedures:

- `CALL ic1(...)` … `CALL ic14(...)` — Interactive Complex
- `CALL is1(...)` … `CALL is7(...)` — Interactive Short

Load it with `LOAD ldbc`. The demo scalar `wiggle()` from the upstream template is still registered.

## Layout

| Path | Purpose |
|------|---------|
| `extension_config.cmake` | Registers this repo with NeuG (`neug_extension_load`) |
| `CMakeLists.txt` | Extension build rules (`EXTENSION_NAME=ldbc`) |
| `include/ldbc_common.h` | Shared graph traversal helpers |
| `include/ic*.h`, `include/is*.h` | Per-query function declarations |
| `include/wiggle_function.h` | Demo scalar `wiggle()` (from upstream template) |
| `src/ic*.cc`, `src/is*.cc` | Per-query implementations |
| `src/ldbc_common.cc` | Shared helpers |
| `src/ldbc_extension.cc` | `Init()` / `Name()` — registers IC/IS + `wiggle()` |
| `test/wiggle_test.cc` | Unit tests for `wiggle()` |
| `test/` | GTest unit tests and Python smoke tests |

## Prerequisites

- CMake >= 3.16, C++20 compiler, OpenSSL dev headers
- NeuG source (git submodule `./neug`, or a sibling/`NEUG_SRCDIR` checkout)

## Build

NeuG is vendored as a git submodule:

```sh
git submodule update --init --recursive
make
```

Layout after init:

```text
neug-extension-template/
  neug/                      # submodule (alibaba/neug@main)
  Makefile
  ...
```

Alternatively, point at an existing NeuG tree:

```sh
NEUG_SRCDIR=/path/to/neug make
# or place a checkout at ../neug
```

The Makefile resolves NeuG in order: `NEUG_SRCDIR` → `./neug` submodule → `../neug` sibling.

Artifacts:

```text
build/release/extension/ldbc/libldbc.neug_extension
build/release/ldbc_extension_test          # when BUILD_TEST=ON
```

Faster rebuilds:

```sh
GEN=ninja EXTRA_CMAKE_FLAGS=-G Ninja make
```

## CI

GitHub Actions clones NeuG into `./neug` before `make` (see `.github/workflows/MainDistributionPipeline.yml`).

This extension needs NeuG with out-of-tree extension support (`cmake/neug_extension.cmake`, `NEUG_EXTENSION_CONFIGS`).

Manual workflow dispatch can override `neug_repository` and `neug_ref`.

## Tests

```sh
make test
```

Python smoke test (requires a built NeuG Python binding):

```sh
NEUG_BUILD_DIR=/path/to/neug/build python3 -m pytest test/python/ -v
```

## Usage

### Standalone

```python
import os
from neug import Database

os.environ["NEUG_EXTENSION_HOME_PYENV"] = os.path.abspath("build/release")
db = Database(":memory:")
conn = db.connect()
conn.execute("LOAD ldbc")
conn.execute("RETURN wiggle('Sam')")  # demo scalar from the template
conn.execute("CALL ic2($personId, $maxDate)", parameters={...})
```

### With LDBC driver

Point the LDBC benchmark repo at this build:

```bash
export NEUG_WORKSPACE=/path/to/workspace   # parent of neug/ and neug-extension-template/
export NEUG_EXTENSION_LIB=$NEUG_WORKSPACE/neug-extension-template/build/release/extension/ldbc/libldbc.neug_extension
cd ldbc && scripts/start_server.sh
```

The LDBC `queries/*.cypher` files call `CALL icN(...)` / `CALL isN(...)`.

## NeuG version

Extension binaries must be built against the same NeuG version as the runtime. See [docs/UPDATING.md](docs/UPDATING.md).

## Template origin

This repo started from the NeuG extension template (`wiggle`). The extension name is now `ldbc`.
