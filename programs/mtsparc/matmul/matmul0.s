    .file "matmul0.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    ! Maximum matrix width (only square matrices supported)
    .equ MAX_N, 16

!
! Multiply A by B and store result in C. Linear version.
!
! %11 = N

    .text
    .globl main
main:
	
	!	for (j=jN=0; j<N; j++, jN+=N)
	clr %3
	clr %4
L3:
    cmp %3, %11
	beq L4
	!	{
	!		for (i=0; i<N; i++)
	clr %2
L5:
    cmp %2, %11
	beq L6
	!		{
	!			for (v=k=kN=0; k<N; k++, kN+=N)
	clr %5
	clr %6
	clr %7
L7:
    cmp %6, %11
	beq L8
	!		    	v += A[kN+i] * B[jN+k];
	set A, %1
	add %7, %2, %0
	sll %0,  2, %9
	add %9, %1, %1
	ld [%1], %8
	set B, %1
	add %4, %6, %0
	add %9, %1, %1
	ld [%1], %9
	smul %9, %8, %1
	add %5, %1, %5
	add %6,  1, %6
	add %7, %11, %7
	jmp L7
L8:
	!			C[jN+i] = v;
	set C, %1
	add %4, %2, %9
	sll %9,  2, %9
	add %9, %1, %1
	st %5, [%1]
	!		}
	add %2,  1, %2
	jmp L5
L6:
	!	}
	add %3, 1, %3
	add %4, %11, %4
	jmp L3
L4:
	!	return 0
	nop
	end

!
! Matrix data
!
    .data
    .align 64
C:  .skip MAX_N * MAX_N * 4

    .section .rodata
    .align 64
A:  .skip MAX_N * MAX_N * 4
    .align 64
B:  .skip MAX_N * MAX_N * 4

