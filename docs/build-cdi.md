Building a CDI
=====

Building a CDI with a PlayStation game included requires some extra steps.

```
cd /path/to/bloom
mkdir build
cd build
kos-cmake ..
make
kos-objcopy -O binary bloom.elf bloom.bin
/opt/toolschains/dc/kos/utils/scramble/scramble bloom.bin 1ST_READ.ELF
```

## Using PlayStation disc image files
```
mkdcdisc -B 1ST_READ.BIN -f path/to/game.cue -f path/to/game.bin -o bloom.cdi
```
---
## Using a folder path

```
mkdcdisc -B 1ST_READ.BIN -D path/to/game/folder -o bloom.cdi
```

The resulting cdi can be burned to a CD with your software of choice.
