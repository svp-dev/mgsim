    .file "matmul0.s"

    # Matrix width (only square matrices supported)
    .equ N, 10

#
# Multiply matrixA by matrixB and store result in matrixC. Linear version.
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
	cmplt $3, N, $0
	beq $0, L4
	#	{
	#		for (i=0; i<N; i++)
	clr $2
L5:
	cmplt $2, N, $0
	beq $0, L6
	#		{
	#			for (v=k=kN=0; k<N; k++, kN+=N)
	clr $5
	clr $6
	clr $7
L7:
	cmplt $6, N, $0
	beq $0, L8
	#		    	v += matrixA[kN+i] * matrixB[jN+k];
	ldah $1, matrixA($29)   !gprelhigh
	lda  $1, matrixA($1)    !gprellow
	addl $7, $2, $0
	s4addl $0, $1, $1
	ldl $8, 0($1)
	ldah $1, matrixB($29)   !gprelhigh
	lda  $1, matrixB($1)    !gprellow
	addl $4, $6, $0
	s4addl $0, $1, $1
	ldl $0, 0($1)
	mull $0, $8, $1
	addl $5, $1, $5
	addl $6, 1, $6
	addl $7, N, $7
	br $31, L7
L8:
	#			matrixC[jN+i] = v;
	ldah $1, matrixC($29)   !gprelhigh
	lda  $1, matrixC($1)    !gprellow
	addl $4, $2, $0
	s4addl $0, $1, $1
	stl $5, 0($1)
	#		}
	addl $2, 1, $2
	br $31, L5
L6:
	#	}
	addl $3, 1, $3
	addl $4, N, $4
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
matrixC:
    .skip N*N*4
    
    .section .rodata

    .align 6
matrixA:
    .rep N*N
    .int 2
    .endr

    .align 6
matrixB:
    .rep N*N
    .int 3
    .endr
