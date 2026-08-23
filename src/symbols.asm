; Addresses discovered by reverse engineering, in asar syntax.
; Regenerate the derived parts with:  make symbols

; --- entry points ---------------------------------------------------------
!Reset                = $80FF70
!Boot_Init            = $80803A
!NMI_Handler          = $808000
!IRQ_Handler          = $80801F
!MainLoop             = $808056
!MainLoop_WaitVBlank  = $80805C

; --- dispatch tables ------------------------------------------------------
!GameModeTable_Main   = $808197   ; 15 x word, indexed by Game_Mode
!GameModeTable_NMI    = $8081BF   ; 15 x word, indexed by Game_Mode
!IrqHandlerTable      = $808B12   ;  6 x word, indexed by Irq_Index

; --- RAM / direct page ----------------------------------------------------
!Frame_Counter        = $34       ; 16-bit, incremented every NMI
!Game_Mode            = $36       ; mode index * 2
!VBlank_Flag          = $44       ; set by NMI, spun on by the main loop
!Irq_Index            = $D0       ; index * 2 into IrqHandlerTable
