	.globl	main
	.ent	main
main:
	# make sure r0 and beq work
	beq	$0, $0, good
bad:
	# abort
	break	0x0, 0x90
good:
	# make sure register writes/reads, bne and nop work
	li	$8, 5
	bne	$9, $8, bad
	# exit
	break	0x0, 0xd0
	.end	main
	.ascii "\0TEST_INPUTS:R8:5\0"
