# Asset re-import

Drop a file named `<table>_<index>.bin` here holding the **decompressed**
bytes of that asset. `make build` re-compresses it and writes it back:

* if the new stream is no larger than the original, it is written in place;
* otherwise it is relocated into free space and the pointer table is updated.

Get a starting file with:

    ./tools/smk assets export palette 0 -o assets/import/palette_0.bin

Table names come from `./tools/smk assets list`.
