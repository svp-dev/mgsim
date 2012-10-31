	.globl	main
	.ent	main
main:
	# make sure r0 and beq work
	beq	$0, $0, good
bad:
	# abort
	sw      $0, 0x268($0)
good:
	# make sure register writes/reads, bne and nop work
	li	$8, 5
	bne	$9, $8, bad
	# exit
	sw      $0, 0x26c($0)
        nop
        nop
        nop
        nop
        nop
	.end	main
        .section .rodata
	.ascii "\0TEST_INPUTS:R8:5\0"
