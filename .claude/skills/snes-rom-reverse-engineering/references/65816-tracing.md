# 65816 facts a tracer needs

## Addressing modes and operand sizes

| mode | syntax | operand bytes |
|---|---|---|
| imp / acc | `nop`, `asl a` | 0 |
| imm8 | `rep #$30` | 1 |
| immM | `lda #…` | **1 if M=1 else 2** |
| immX | `ldx #…`, `ldy #…`, `cpx`, `cpy` | **1 if X=1 else 2** |
| dp, dpx, dpy, idp, idx, idy, idl, idly, sr, sry | `lda $12,x` | 1 |
| abs, abx, aby, ind, iax, ial, rell | `lda $1234,x` | 2 |
| bm (`mvn`/`mvp`) | `mvn $dst,$src` | 2 |
| abl, alx | `lda $123456,x` | 3 |
| rel | `bne` | 1 |

`BIT #imm` is immM. `PEA` takes a 16-bit immediate regardless of M.
`BRK`/`COP`/`WDM` each take a signature byte.

## Flag effects to model

| instruction | effect on the traced state |
|---|---|
| `REP #n` | `n & $20` clears M; `n & $10` clears X |
| `SEP #n` | `n & $20` sets M; `n & $10` sets X |
| `XCE` | entering native mode forces M=1, X=1 |
| `PHP` | push `(M,X)` onto a per-path stack |
| `PLP` | pop it; if the stack is empty the state is unknown |
| `RTI` | flags restored from the stack — terminate the path |

Cap the PHP stack (8 deep is plenty) so pathological code cannot explode the
state space.

## Control flow classification

- **terminators** — `RTS RTL RTI STP JMP JML BRA BRL` (no fallthrough)
- **conditional branches** — `BPL BMI BVC BVS BCC BCS BNE BEQ` (both edges)
- **calls** — `JSR JSL`

Targets:

```
rel   : target = bank | ((addr + 2 + s8(operand))  & $FFFF)
rell  : target = bank | ((addr + 3 + s16(operand)) & $FFFF)
JSR/JMP abs : target = bank | operand          (stays in the program bank)
JSL/JML long: target = operand                 (full 24-bit)
JMP (abs) / JSR (abs,x) / JML [abs] : unresolvable — record as indirect
```

The program counter wraps **within** a bank: `next = (addr & $FF0000) |
((addr + size) & $FFFF)`.

## Junk opcodes

`BRK $00`, `COP $02`, `WDM $42`, `STP $DB` essentially never appear in
shipped game code. Their frequency is the best cheap signal that a trace has
desynchronised or wandered into data. `$00` and `$FF` filler decode as
`BRK` and `SBC $xxxxxx,x`, so data traced as code lights this up immediately.
