; ---------------------------------------------------------------------------
; Super Mario Kart - patch root
;
; This file and everything it includes contains ONLY original code written for
; this project.  It is applied on top of a ROM you supply yourself; no
; copyrighted material from the game is stored in this repository.
; ---------------------------------------------------------------------------
hirom

incsrc "symbols.asm"        ; addresses discovered by reverse engineering

; --- freespace ------------------------------------------------------------
; Nothing is placed here until a patch asks for it.  Use `freecode` /
; `freedata` so asar finds room instead of hardcoding an offset.

; --- patches --------------------------------------------------------------
; Add one incsrc line per patch.  Each patch is self-contained and reversible
; by commenting out its line.
incsrc "patches/00_example_disabled.asm"
