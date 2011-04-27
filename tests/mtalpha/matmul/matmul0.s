    .file "matmul0.s"
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    # Maximum matrix width (only square matrices supported)
    .equ MAX_N, 16

#
# Multiply A by B and store result in C. Linear version.
#
# $27 = main
# $10 = N
# 
    .text
    .ent main
    .globl main
main:
    # Load GP register
	ldah $29,0($27)     !gpdisp!1
	lda  $29,0($29)     !gpdisp!1
	
	#	for (j=jN=0; j<N; j++, jN+=N)
	clr $3
	clr $4
L3:
	cmplt $3, $10, $0
	beq $0, L4
	#	{
	#		for (i=0; i<N; i++)
	clr $2
L5:
	cmplt $2, $10, $0
	beq $0, L6
	#		{
	#			for (v=k=kN=0; k<N; k++, kN+=N)
	clr $5
	clr $6
	clr $7
L7:
	cmplt $6, $10, $0
	beq $0, L8
	#		    	v += A[kN+i] * B[jN+k];
	ldah $1, A($29)   !gprelhigh
	lda  $1, A($1)    !gprellow
	addl $7, $2, $0
	s4addl $0, $1, $1
	ldl $8, 0($1)
	ldah $1, B($29)   !gprelhigh
	lda  $1, B($1)    !gprellow
	addl $4, $6, $0
	s4addl $0, $1, $1
	ldl $0, 0($1)
	mull $0, $8, $1
	addl $5, $1, $5
	addl $6, 1, $6
	addl $7, $10, $7
	br $31, L7
L8:
	#			C[jN+i] = v;
	ldah $1, C($29)   !gprelhigh
	lda  $1, C($1)    !gprellow
	addl $4, $2, $0
	s4addl $0, $1, $1
	stl $5, 0($1)
	#		}
	addl $2, 1, $2
	br $31, L5
L6:
	#	}
	addl $3, 1, $3
	addl $4, $10, $4
	br $31, L3
L4:
	#	return 0
	addl $0, $31, $31
	end
    .end main

#
# Matrix data
#
    .data
    .align 6;
C:  .skip MAX_N * MAX_N * 4
    
    .section .rodata
    .align 6
A:  .skip MAX_N * MAX_N * 4
    .align 6
B:  .skip MAX_N * MAX_N * 4
