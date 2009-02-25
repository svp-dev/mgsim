    .file "matmul0.s"

    # Matrix width (only square matrices supported)
    .equ N, 10

#
# Multiply matrixA by matrixB and store result in matrixC. Linear version.
#
    .text
    .globl main
main:
	
	#	for (j=jN=0; j<N; j++, jN+=N)
	clr %3
	clr %4
L3:
    cmp %3, N
	beq L4
	#	{
	#		for (i=0; i<N; i++)
	clr %2
L5:
    cmp %2, N
	beq L6
	#		{
	#			for (v=k=kN=0; k<N; k++, kN+=N)
	clr %5
	clr %6
	clr %7
L7:
    cmp %6, N
	beq L8
	#		    	v += matrixA[kN+i] * matrixB[jN+k];
	set matrixA, %1
	add %7, %2, %0
	sll %0,  2, %9
	add %9, %1, %1
	ld [%1], %8
	set matrixB, %1
	add %4, %6, %0
	add %9, %1, %1
	ld [%1], %9
	smul %9, %8, %1
	add %5, %1, %5
	add %6,  1, %6
	add %7,  N, %7
	jmp L7
L8:
	#			matrixC[jN+i] = v;
	set matrixC, %1
	add %4, %2, %9
	sll %9,  2, %9
	add %9, %1, %1
	st %5, [%1]
	#		}
	add %2,  1, %2
	jmp L5
L6:
	#	}
	add %3, 1, %3
	add %4, N, %4
	jmp L3
L4:
	#	return 0
	nop
	end

#
# Matrix data
#
    .data
    .align 64
matrixC:
    .skip N*N*4
    
    .section .rodata

    .align 64
matrixA:
    .rep N*N
    .int 2
    .endr

    .align 64
matrixB:
    .rep N*N
    .int 4
    .endr
