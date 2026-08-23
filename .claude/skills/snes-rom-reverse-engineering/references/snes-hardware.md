# SNES facts a native port needs

## Timing

| | NTSC | PAL |
|---|---|---|
| frame (vblank/NMI) rate | **60.0988 Hz** | 50.007 Hz |
| master clock | 21477272.7 Hz | 21281370 Hz |
| lines per frame | 262 | 312 |
| dots per line | 1364 | 1364 |

A main loop that clears a flag, spins until NMI sets it, then runs one mode
handler is doing exactly one simulation step per frame. That is the tick.

## The DMA registers, and what each tells you

| register | meaning | what it reveals |
|---|---|---|
| `$4300+n0` DMAP | transfer pattern | `$00` one byte to one register, `$01` two bytes to two registers |
| `$4301+n0` BBAD | destination `$21xx` | `$18` VMDATAL, `$19` VMDATAH, `$22` CGDATA (palette), `$04` OAMDATA (sprites), `$80` WMDATA (WRAM) |
| `$4302+n0` A1T | source address | where the asset is |
| `$4304+n0` A1B | source bank | `$7E`/`$7F` means it was decompressed to WRAM first |
| `$4305+n0` DAS | length | **the asset's exact size** |
| `$2115` VMAIN | VRAM increment | bit 7 set = increment after the `$2119` (high byte) write |

`BBAD = $18` with `DMAP = $00` writes only low bytes; `BBAD = $19` writes only
high bytes. In Mode 7 that separates the tilemap from the tile pixels.

## Mode 7

- VRAM is interleaved: **low byte = tilemap entry, high byte = tile pixel**.
- The tilemap is 128×128 bytes = 16384; tiles are 8×8, linear 8bpp, 64 bytes.
- So a full Mode 7 screen is a 1024×1024 pixel plane that wraps.
- `$211B`–`$211E` are the affine matrix M7A–M7D, `$211F`/`$2120` the centre.
  Games rewrite these per scanline with HDMA to fake perspective.

A native port should compute the ground plane directly instead; see the skill.

## Colour

BGR555, one 16-bit word per colour, **bit 15 unused and always clear**:

```
r = v & 0x1F; g = (v >> 5) & 0x1F; b = (v >> 10) & 0x1F
scale 5->8 bits as (c * 255 + 15) / 31, not c << 3
```

A 512-byte blob whose odd bytes all have bit 7 clear is a 256-colour palette.
That check is decisive — 256 words agreeing by chance is not a thing.

## Memory

- WRAM is banks `$7E`–`$7F`, 128 KB. Decompressors usually target it, then DMA
  to VRAM.
- VRAM is 64 KB, addressed as 32768 **words** through `$2116`/`$2118`/`$2119`.
- CGRAM is 512 bytes: 256 colours.
- OAM is 544 bytes: 128 sprites.
