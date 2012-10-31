        .text
	.globl	main
	.ent	main
main:
	li	$2, 5
	# check addu
	addu	$2, $2, $2
	li	$3, 10
	bne	$2, $3, bad
	# check addiu
	addiu	$2, 8
	li	$3, 18
	bne	$2, $3, bad
	# check subu
	li	$3, 6
	subu	$2, $2, $3
	li	$3, 12
	bne	$2, $3, bad
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
