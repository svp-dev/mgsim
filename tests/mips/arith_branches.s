	.globl	main
	.ent	main
	.set	noat
main:
	# j
	j	step1
	beqz	$0, bad

step1:
	# R-type basic arithmetic
	# add, sub, and, or (and bne/beq)
	li	$2, 9
	add	$2, $2, $2	# 9 + 9 = 18
	li	$3, 6
	sub	$2, $2, $3	# 18 - 6 = 12
	li	$3, 6
	and	$2, $2, $3	# 12 & 6 = 4
	li	$3, 1
	or	$2, $2, $3	# 4 | 1 = 5
	li	$3, 5
	bne	$2, $3, bad
	beq	$2, $3, step2
	j	bad
step2:
	# xor, nor (and bgtz/blez)
	li	$3, 8
	xor	$2, $2, $3	# 5 ^ 8 = 13
	li	$3, 16
	nor	$2, $2, $3	# 13 nor 16 = 0xffffffe2
	li	$3, 0xffffffe2
	bne	$2, $3, bad
	bgtz	$2, bad
	blez	$2, step3
	j	bad
step3:
	# slt, sltu (and beqz/bnez)
	li	$2, -5
	li	$3, 6
	slt	$1, $2, $3
	beqz	$1, bad		# -5 < 6
	sltu	$1, $2, $3
	bnez	$1, bad		# 0xfffffffb > 6

	# R-type shifts
	# sll, srl
	li	$2, 1
	sll	$2, $2, 5
	srl	$2, $2, 3
	li	$3, 4
	bne	$2, $3, bad
	# sllv, srlv
	li	$3, 2
	sllv	$2, $2, $3
	li	$3, 1
	srlv	$2, $2, $3
	li	$3, 8
	bne	$2, $3, bad
	# sra, srav
	li	$2, -16
	sra	$2, $2, 1
	li	$3, 2
	srav	$2, $2, $3
	li	$3, -2
	bne	$2, $3, bad

	# I-type basic arithmetic
	# addi, addiu, andi, ori, xori
	li	$2, 1
	addi	$2, 2
	addiu	$2, 3
	andi	$2, 5
	ori	$2, 8
	xori	$2, 16
	li	$3, 28
	bne	$2, $3, bad

	# lui
	lui	$2, 5
	srl	$2, $2, 16
	li	$3, 5
	bne	$2, $3, bad

	# slti, sltiu
	li	$2, -5
	slti	$3, $2, 4
	beqz	$3, bad
	sltiu	$3, $2, 4
	bnez	$3, bad

	li	$3, 0x80

	# REGIMM
	# bgez, bltz, bltzal, bgezal
	li	$2, -2
	bgez	$2, bad
	li	$2, 0
	bltz	$2, bad
	li	$2, -2
	bltzal	$2, test_jal
	bne	$2, $3, bad
	li	$31, 0
	li	$2, 2
prev_call:
	bgezal	$2, test_jal
	bne	$2, $3, bad

	# jalr
	addiu	$2, $31, test_jal-prev_call-4
	jalr	$2
	bne	$2, $3, bad
	li	$31, 0

	# J-type (jal, jr)
	jal	test_jal
	j	done_jal
test_jal:
	li	$2, 0x80
	nop
	jr	$31
done_jal:
	bne	$2, $3, bad

	# exit
	break	0x0, 0xd0
bad:
	break	0x0, 0x90
	.end	main
