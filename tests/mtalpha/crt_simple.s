# crt_simple.s: this file is part of the Microgrid simulator.
#
# Copyright (C) 2011 the Microgrid project.
#
# This program is free software, you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, either version 2
# of the License, or (at your option) any later version.
#
# The complete GNU General Public Licence Notice can be found as the
# `COPYING' file in the root directory.
#

	.text
	.ent _start
	.globl _start
	.registers 0 0 31 0 0 31
_start:
        ldpc $l14
	ldgp $l17, 0($l14)
	ldfp $l18
	mov $l18, $l16 # set up frame pointer
        
	clr $l8 # flush callee-save reg
	clr $l9 # flush callee-save reg
	clr $l10 # flush callee-save reg
	clr $l11 # flush callee-save reg
	clr $l12 # flush callee-save reg
	clr $l13 # flush callee-save reg
	fclr $lf11 # flush callee-save reg
	fclr $lf12 # flush callee-save reg
	fclr $lf13 # flush callee-save reg
	fclr $lf14 # flush callee-save reg
	fclr $lf15 # flush callee-save reg
	fclr $lf16 # flush callee-save reg
	fclr $lf17 # flush callee-save reg
	fclr $lf18 # flush callee-save reg
	fclr $lf3 # init FP return reg
        clr $l2 # flush arg reg
	clr $l3 # flush arg reg
        clr $l4 # flush arg reg
        clr $l5 # flush arg reg
        clr $l6 # flush arg reg
        clr $l7 # flush arg reg
	fclr $lf5 # flush arg reg
	fclr $lf6 # flush arg reg
	fclr $lf7 # flush arg reg
	fclr $lf8 # flush arg reg
	fclr $lf9 # flush arg reg
	fclr $lf10 # flush arg reg
        
	# call test(void)
	ldq $l14,test($l17) !literal!2
	jsr $l15,($l14),test !lituse_jsr!2
        swch
	ldgp $l17,0($l15)

	bne $l1, $bad
        swch
        nop
	end
$bad:
	ldah $l3, $msg($l17) !gprelhigh
	lda $l2, 115($l31)  # char <- 's'
	lda $l3, $msg($l3) !gprellow
	.align 4
$L0:
	print $l2, 194  # print char -> stderr
	lda $l3, 1($l3)
	ldbu $l2, 0($l3)
	sextb $l2, $l2
        swch
	bne $l2, $L0
        swch

	print $l1, 130 # print int -> stderr
        lda $l1, 10($l31) # NL
	print $l1, 194  # print char -> stderr
$fini:	
	nop
	nop
	pal1d 0
	br $fini
        swch
	.end _start

	.section .rodata
$msg:	
	.ascii "slrt: main returned \0"


        
