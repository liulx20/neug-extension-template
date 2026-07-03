# Updating NeuG

Extension binaries must match the NeuG version used at runtime. Rebuild after changing NeuG.

## Sibling checkout (recommended)

If NeuG lives at `../neug` relative to this repo:

```bash
cd ../neug
git pull
cd ../neug-extension-template
make clean && make test
```

Or pin to a specific commit/tag in `../neug`, then rebuild.

## Git submodule

If you use the optional `neug` submodule:

```bash
git submodule update --init --recursive
cd neug
git fetch --tags
git checkout <tag-or-commit>
cd ..
make clean && make test
git add neug
git commit -m "Pin NeuG to <tag-or-commit>"
```

## Override NeuG location

```bash
NEUG_SRCDIR=/path/to/neug make
```
