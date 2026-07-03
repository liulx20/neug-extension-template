# Wiggle

This repository is based on https://github.com/alibaba/neug-extension-template — use it as a starting point to build and ship your own NeuG extension.

---

This extension, **Wiggle**, demonstrates how to register a custom scalar function in NeuG.

## Building

```sh
make
```

Artifacts:

```sh
./build/release/extension/wiggle/libwiggle.neug_extension
```

## Usage

```python
import os
from neug import Database

os.environ["NEUG_EXTENSION_HOME_PYENV"] = os.path.abspath("build/release")
db = Database(":memory:")
conn = db.connect()
conn.execute("LOAD wiggle")
conn.execute("RETURN wiggle('Jane')")
```

## Tests

```sh
make test
```
