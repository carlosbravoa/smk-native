# Base ROM

Place your own, legally-obtained copy of **Super Mario Kart (USA)** here as:

    rom/smk_usa.sfc

Expected dump (headerless, 512 KB):

| field  | value |
|--------|-------|
| sha1   | `47e103d8398cf5b7cbb42b95df3a3c270691163b` |
| md5    | `7f25ce5a283d902694c52fb1152fa61a` |
| crc32  | `CD80DB86` |
| size   | 524288 bytes (no 512-byte copier header) |

If your file is 524800 bytes it has a copier header; the tools strip it
automatically in memory, but `make verify` will report the on-disk hash.

Check it with:

    make verify

No ROM, and no data extracted from one, is committed to this repository.
