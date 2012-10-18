	.globl	main
	.ent	main
main:
	# multu
	li	$4, 0x10001
	li	$5, 0x20002
	multu	$4, $5
	mflo	$3
	bne	$3, 0x40002, bad
	mfhi	$3
	bne	$3, 0x2, bad

	# divu
	li	$4, 200
	li	$5, 9
	divu	$0, $4, $5
	mflo	$3
	bne	$3, 22, bad
	mfhi	$3
	bne	$3, 2, bad

	# mtlo/mthi
	mtlo	$4
	mthi	$5
	mflo	$3
	bne	$3, 200, bad
	mfhi	$3
	bne	$3, 9, bad

	# exit
	break	0x0, 0xd0
bad:
	break	0x0, 0x90
	.end	main
