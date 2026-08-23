; Example patch, intentionally a no-op so a fresh checkout rebuilds a ROM
; byte-identical to the base (minus the checksum, which is recomputed).
;
; To write a real patch:
;   org $80803C
;       stz.w $4200        ; overwrite an instruction in place, OR
;
;   freecode               ; let asar find unused space
;   MyNewRoutine:
;       ...
;       rtl
