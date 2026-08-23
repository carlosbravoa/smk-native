"""Complete 65816 opcode table.

Addressing modes:
  imp acc imm8 immM immX dp dpx dpy idp idx idy idl idly
  abs abx aby abl alx ind iax ial rel rell sr sry bm
"""
from __future__ import annotations

# (mnemonic, mode)
OPCODES: list[tuple[str, str]] = [None] * 256  # type: ignore

_T = """
00 BRK imm8 | 01 ORA idx  | 02 COP imm8 | 03 ORA sr   | 04 TSB dp   | 05 ORA dp   | 06 ASL dp   | 07 ORA idl
08 PHP imp  | 09 ORA immM | 0A ASL acc  | 0B PHD imp  | 0C TSB abs  | 0D ORA abs  | 0E ASL abs  | 0F ORA abl
10 BPL rel  | 11 ORA idy  | 12 ORA idp  | 13 ORA sry  | 14 TRB dp   | 15 ORA dpx  | 16 ASL dpx  | 17 ORA idly
18 CLC imp  | 19 ORA aby  | 1A INC acc  | 1B TCS imp  | 1C TRB abs  | 1D ORA abx  | 1E ASL abx  | 1F ORA alx
20 JSR abs  | 21 AND idx  | 22 JSL abl  | 23 AND sr   | 24 BIT dp   | 25 AND dp   | 26 ROL dp   | 27 AND idl
28 PLP imp  | 29 AND immM | 2A ROL acc  | 2B PLD imp  | 2C BIT abs  | 2D AND abs  | 2E ROL abs  | 2F AND abl
30 BMI rel  | 31 AND idy  | 32 AND idp  | 33 AND sry  | 34 BIT dpx  | 35 AND dpx  | 36 ROL dpx  | 37 AND idly
38 SEC imp  | 39 AND aby  | 3A DEC acc  | 3B TSC imp  | 3C BIT abx  | 3D AND abx  | 3E ROL abx  | 3F AND alx
40 RTI imp  | 41 EOR idx  | 42 WDM imm8 | 43 EOR sr   | 44 MVP bm   | 45 EOR dp   | 46 LSR dp   | 47 EOR idl
48 PHA imp  | 49 EOR immM | 4A LSR acc  | 4B PHK imp  | 4C JMP abs  | 4D EOR abs  | 4E LSR abs  | 4F EOR abl
50 BVC rel  | 51 EOR idy  | 52 EOR idp  | 53 EOR sry  | 54 MVN bm   | 55 EOR dpx  | 56 LSR dpx  | 57 EOR idly
58 CLI imp  | 59 EOR aby  | 5A PHY imp  | 5B TCD imp  | 5C JML abl  | 5D EOR abx  | 5E LSR abx  | 5F EOR alx
60 RTS imp  | 61 ADC idx  | 62 PER rell | 63 ADC sr   | 64 STZ dp   | 65 ADC dp   | 66 ROR dp   | 67 ADC idl
68 PLA imp  | 69 ADC immM | 6A ROR acc  | 6B RTL imp  | 6C JMP ind  | 6D ADC abs  | 6E ROR abs  | 6F ADC abl
70 BVS rel  | 71 ADC idy  | 72 ADC idp  | 73 ADC sry  | 74 STZ dpx  | 75 ADC dpx  | 76 ROR dpx  | 77 ADC idly
78 SEI imp  | 79 ADC aby  | 7A PLY imp  | 7B TDC imp  | 7C JMP iax  | 7D ADC abx  | 7E ROR abx  | 7F ADC alx
80 BRA rel  | 81 STA idx  | 82 BRL rell | 83 STA sr   | 84 STY dp   | 85 STA dp   | 86 STX dp   | 87 STA idl
88 DEY imp  | 89 BIT immM | 8A TXA imp  | 8B PHB imp  | 8C STY abs  | 8D STA abs  | 8E STX abs  | 8F STA abl
90 BCC rel  | 91 STA idy  | 92 STA idp  | 93 STA sry  | 94 STY dpx  | 95 STA dpx  | 96 STX dpy  | 97 STA idly
98 TYA imp  | 99 STA aby  | 9A TXS imp  | 9B TXY imp  | 9C STZ abs  | 9D STA abx  | 9E STZ abx  | 9F STA alx
A0 LDY immX | A1 LDA idx  | A2 LDX immX | A3 LDA sr   | A4 LDY dp   | A5 LDA dp   | A6 LDX dp   | A7 LDA idl
A8 TAY imp  | A9 LDA immM | AA TAX imp  | AB PLB imp  | AC LDY abs  | AD LDA abs  | AE LDX abs  | AF LDA abl
B0 BCS rel  | B1 LDA idy  | B2 LDA idp  | B3 LDA sry  | B4 LDY dpx  | B5 LDA dpx  | B6 LDX dpy  | B7 LDA idly
B8 CLV imp  | B9 LDA aby  | BA TSX imp  | BB TYX imp  | BC LDY abx  | BD LDA abx  | BE LDX aby  | BF LDA alx
C0 CPY immX | C1 CMP idx  | C2 REP imm8 | C3 CMP sr   | C4 CPY dp   | C5 CMP dp   | C6 DEC dp   | C7 CMP idl
C8 INY imp  | C9 CMP immM | CA DEX imp  | CB WAI imp  | CC CPY abs  | CD CMP abs  | CE DEC abs  | CF CMP abl
D0 BNE rel  | D1 CMP idy  | D2 CMP idp  | D3 CMP sry  | D4 PEI idp  | D5 CMP dpx  | D6 DEC dpx  | D7 CMP idly
D8 CLD imp  | D9 CMP aby  | DA PHX imp  | DB STP imp  | DC JML ial  | DD CMP abx  | DE DEC abx  | DF CMP alx
E0 CPX immX | E1 SBC idx  | E2 SEP imm8 | E3 SBC sr   | E4 CPX dp   | E5 SBC dp   | E6 INC dp   | E7 SBC idl
E8 INX imp  | E9 SBC immM | EA NOP imp  | EB XBA imp  | EC CPX abs  | ED SBC abs  | EE INC abs  | EF SBC abl
F0 BEQ rel  | F1 SBC idy  | F2 SBC idp  | F3 SBC sry  | F4 PEA abs  | F5 SBC dpx  | F6 INC dpx  | F7 SBC idly
F8 SED imp  | F9 SBC aby  | FA PLX imp  | FB XCE imp  | FC JSR iax  | FD SBC abx  | FE INC abx  | FF SBC alx
"""

for _line in _T.strip().splitlines():
    for _cell in _line.split("|"):
        _p = _cell.split()
        if len(_p) == 3:
            OPCODES[int(_p[0], 16)] = (_p[1], _p[2])
assert all(o is not None for o in OPCODES), "opcode table incomplete"

# operand byte count per mode (immM/immX resolved at decode time)
SIZES = {
    "imp": 0, "acc": 0, "imm8": 1, "dp": 1, "dpx": 1, "dpy": 1, "idp": 1,
    "idx": 1, "idy": 1, "idl": 1, "idly": 1, "sr": 1, "sry": 1, "rel": 1,
    "abs": 2, "abx": 2, "aby": 2, "ind": 2, "iax": 2, "ial": 2, "rell": 2,
    "bm": 2, "abl": 3, "alx": 3,
}

FORMAT = {
    "imp": "", "acc": " A", "imm8": " #${0:02X}", "dp": " ${0:02X}",
    "dpx": " ${0:02X},x", "dpy": " ${0:02X},y", "idp": " (${0:02X})",
    "idx": " (${0:02X},x)", "idy": " (${0:02X}),y", "idl": " [${0:02X}]",
    "idly": " [${0:02X}],y", "sr": " ${0:02X},s", "sry": " (${0:02X},s),y",
    "abs": " ${0:04X}", "abx": " ${0:04X},x", "aby": " ${0:04X},y",
    "ind": " (${0:04X})", "iax": " (${0:04X},x)", "ial": " [${0:04X}]",
    "abl": " ${0:06X}", "alx": " ${0:06X},x",
}

# control-flow classification
TERMINATORS = {"RTS", "RTL", "RTI", "STP", "JMP", "JML", "BRA", "BRL"}
BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
CALLS = {"JSR", "JSL"}


def operand_size(mode: str, m: int, x: int) -> int:
    if mode == "immM":
        return 1 if m else 2
    if mode == "immX":
        return 1 if x else 2
    return SIZES[mode]
