        .text
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
	sw      $0, 0x26c($0) # exit: action_base + 3 * wordsize
bad:
	sw      $0, 0x268($0) # exit: action_base + 2 * wordsize
        nop
        nop
        nop
        nop
        nop
	.end	main
