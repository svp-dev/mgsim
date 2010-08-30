    .file "matmul2.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    ! Maximum matrix width (only square matrices supported)
    .equ MAX_N, 16

    ! Block sizes, comment lines to not set the block size
    .equ BLOCK1,    5
                
!
! Multiply A by B and store result in C. Double depth uTC version.
!
! %11 = N
!
    .text
    .globl main
main:
	allocate (1 << 3), %5
	
	!	create (fam1; 0; N-1;)
	setlimit %5, %11
	swch
	.ifdef BLOCK1
	setblock %5, BLOCK1
	.endif
	cred thread1, %5
	
	set A, %1
	set B, %2
	set C, %3
	putg %1,  %5, 0
	putg %2,  %5, 1
	putg %3,  %5, 2
	putg %11, %5, 3

	!	sync(fam1);
	sync %5, %1
	release %5
	mov %1, %0
	end


    ! %g0 = A 
    ! %g1 = B
    ! %g2 = C
    ! %g3 = N
    ! %l0 = i
    .align 64
	.registers 4 0 5  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread1:
	allocate (1 << 3), %l4
	setlimit %l4, %g3
	swch
	cred thread2, %l4
	
	umul    %l0, %g3, %l0       ! %l0 = i*N
	sll     %l0,   2, %l0
	add     %l0, %g2, %l2       ! %l2 = &C[i*N]
	add     %l0, %g0, %l0       ! %l0 = &A[i*N]
	putg    %l0, %l4, 0
	putg    %g1, %l4, 1
	putg    %l2, %l4, 2
	putg    %g3, %l4, 3
	
	sync %l4, %l0
	release %l4
	mov %l0, %0
	end


    ! %g0 = &A[i*N]
    ! %g1 = B
    ! %g2 = &C[i*N]
    ! %g3 = N
    ! %l0 = j
    .align 64
	.registers 4 0 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread2:
    sll     %l0,   2, %l1
	add     %l1, %g1, %l1       ! %l1 = &B[j]
	clr     %l2                 ! %l2 = sum = 0

    !
	! for (int k = 0; k < N; k++) {
	!
    clr %l3                     ! %l3 = k
    ba L2e
    swch
L2s:
    sll     %l3,   2, %l4
    add     %l4, %g0, %l4       ! %l4 = &A[i*N+k]
    ld      [%l4], %l4          ! %l4 =  A[i*N+k]
    sll     %g3,   2, %l5
    add     %l5, %l1, %l5       ! %l5 = &B[k*N+j]
    ld      [%l5], %l5          ! %l5 =  B[k*N+j]
    
    smul    %l4, %l5, %l4       ! %l4 = A[i*N+k] * B[k*N+j]
    swch
    
    add     %l2, %l4, %l2       ! %l2 = sum = sum + A[i*N+k] * B[k*N+j]
    
    !
    ! }
    !
    inc     %l3
L2e:cmp     %l3, %g3
	blt     L2s
	swch
	
	sll     %l0,   2, %l0
	add     %l0, %g2, %l0       ! %l0 = &C[i*N+j]
	st      %l2, [%l0]          ! C[i*N+j] = sum
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

