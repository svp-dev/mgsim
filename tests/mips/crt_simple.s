# crt_simple.s: this file is part of the Microgrid simulator
#
# Copyright (C) 2013 the Microgrid project.
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
	.align	2
	.globl	__start
	.set	nomips16
	.ent	__start
	.type	__start, @function
__start:
	.frame	$sp,32,$31		# vars= 0, regs= 1/0, args= 16, gp= 8
	.mask	0x80000000,-4
	.fmask	0x00000000,0
	.set	noreorder
	.cpload	$25
        mfc2    $sp, $4
	addiu	$sp,$sp,-32
        move    $fp,$sp
	sw	$31,28($sp)
	.cprestore	16

	jal	test
        # exit with code
        sw      $2,0x26c($0)
	.end	__start
	.size	__start, .-__start

