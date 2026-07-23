# NeuG Extension Template (upstream docs)

This directory keeps the original template documentation. For this LDBC fork, start with the root [README.md](../README.md).

## Template workflow

This LDBC fork builds the `ldbc` extension (`LOAD ldbc`). The upstream demo scalar `wiggle()` is still included (`include/wiggle_function.h`, `test/wiggle_test.cc`).

### Building

```sh
make
```

Artifacts:

```sh
./build/release/extension/ldbc/libldbc.neug_extension
./build/release/ldbc_extension_test
```

### NeuG source resolution

The Makefile picks NeuG in this order:

1. `NEUG_SRCDIR` environment variable
2. `./neug` git submodule
3. `../neug` sibling directory

### Renaming for a new extension

On a **clean** template checkout (not this LDBC fork):

```sh
python3 ./scripts/bootstrap-template.py <extension_name_in_snake_case>
```

### CLion setup

1. Open `neug/CMakeLists.txt` as the CMake project (or set `NEUG_SRCDIR` to your NeuG tree).
2. Add CMake options:

```
-DNEUG_EXTENSION_CONFIGS=<absolute_path_to>/extension_config.cmake
-DBUILD_EXTENSIONS=ldbc
-DBUILD_PYTHON=OFF
-DBUILD_TEST=ON
```

See also [UPDATING.md](UPDATING.md) for NeuG version management.
