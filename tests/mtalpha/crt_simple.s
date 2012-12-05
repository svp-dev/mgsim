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
        
	# call test(void)
	ldq $l14,test($l17) !literal!2
	jsr $l15,($l14),test !lituse_jsr!2
        swch
	ldgp $l17,0($l15)

        stq $l1, 0x278($31)
	end
	.end _start

