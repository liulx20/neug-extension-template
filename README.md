# LDBC SNB Interactive Extension for NeuG

Out-of-tree NeuG extension that implements LDBC SNB Interactive **read queries** as stored procedures:

- `CALL ic1(...)` … `CALL ic14(...)` — Interactive Complex
- `CALL is1(...)` … `CALL is7(...)` — Interactive Short

The extension name is still `wiggle` (from the upstream [neug-extension-template](https://github.com/alibaba/neug-extension-template)); load it with `LOAD wiggle`.

## Layout

| Path | Purpose |
|------|---------|
| `extension_config.cmake` | Registers this repo with NeuG (`neug_extension_load`) |
| `CMakeLists.txt` | Extension build rules |
| `include/ldbc_common.h` | Shared graph traversal helpers |
| `include/ic*.h`, `include/is*.h` | Per-query function declarations |
| `src/ic*.cc`, `src/is*.cc` | Per-query implementations |
| `src/ldbc_common.cc` | Shared helpers |
| `src/wiggle_extension.cc` | `Init()` / `Name()` entry point |
| `test/` | GTest unit tests and Python smoke tests |

## Prerequisites

- CMake >= 3.16, C++20 compiler, OpenSSL dev headers
- NeuG source tree (sibling checkout or submodule)

## Build

Place NeuG next to this repo (recommended for local dev):

```text
workspace/
  neug/
  neug-extension-template/   # this repo
```

Then from this directory:

```sh
make
```

The Makefile resolves NeuG in order: `NEUG_SRCDIR` env → `./neug` submodule → `../neug` sibling.

Artifacts:

```text
build/release/extension/wiggle/libwiggle.neug_extension
build/release/wiggle_extension_test          # when BUILD_TEST=ON
```

Faster rebuilds:

```sh
GEN=ninja EXTRA_CMAKE_FLAGS=-G Ninja make
```

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
conn.execute("LOAD wiggle")
conn.execute("CALL ic2($personId, $maxDate)", parameters={...})
```

### With LDBC driver

Point the [ldbc](https://github.com/alibaba/neug/tree/neug) benchmark repo at this build:

```bash
export NEUG_WORKSPACE=/path/to/workspace   # parent of neug/ and neug-extension-template/
export NEUG_EXTENSION_LIB=$NEUG_WORKSPACE/neug-extension-template/build/release/extension/wiggle/libwiggle.neug_extension
cd ldbc && scripts/start_server.sh
```

The LDBC `queries/*.cypher` files call `CALL icN(...)` / `CALL isN(...)`.

## NeuG version

Extension binaries must be built against the same NeuG version as the runtime. See [docs/UPDATING.md](docs/UPDATING.md).

## Template origin

This repo started from the NeuG extension template. To fork a **new** extension name from scratch, run `python3 scripts/bootstrap-template.py <name>` on a clean template checkout (not recommended on this LDBC fork).
