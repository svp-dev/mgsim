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
	break	0x0, 0xd0
bad:
	break	0x0, 0x90
	.end	main
