	.comm	_stack, 4096, 4
	.globl	main
	.ent	main
main:
	# stack/gp setup
	.frame	$sp, 32, $31
	.set	noreorder
	.cpload	$25
	.set	reorder
	la	$2, _stack
	addiu	$2, $2, 4092
	move	$sp, $2
	.cprestore 0

	# lb/sb
	la	$5, testbytes
	lb	$2, 0($5)
	bne	$2, 0x2, bad
	lb	$2, 1($5)
	bne	$2, -1, bad
	li	$2, 0xfe
	sb	$2, 0($5)
	lbu	$3, 0($5)
	bne	$3, 0xfe, bad

	# lh/lhu/sh
	la	$5, testhwords
	lhu	$2, 0($5)
	bne	$2, 0x8000, bad
	lh	$2, 2($5)
	bne	$2, 0xffff9000, bad
	li	$2, 0xf000
	sh	$2, 0($5)
	lhu	$3, 0($5)
	bne	$3, 0xf000, bad

	# lw/sw
	la	$5, testwords
	lw	$2, 0($5)
	bne	$2, 0x40000000, bad
	lw	$2, 4($5)
	bne	$2, 0x50000000, bad
	li	$2, 0x90000000
	sw	$2, 0($5)
	lw	$3, 0($5)
	bne	$3, 0x90000000, bad

	# exit
	break	0x0, 0xd0

bad:
	break	0x0, 0x90
	.end	main

	.data
testwords:
	.word	0x40000000
	.word	0x50000000
testhwords:
	.hword	0x8000
	.hword	0x9000
testbytes:
	.byte	0x2
	.byte	0xff
