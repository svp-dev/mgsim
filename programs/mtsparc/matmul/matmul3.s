    .file "matmul3.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    ! Maximum matrix width (only square matrices supported)
    .equ MAX_N, 16
    
    ! Block sizes, comment lines to not set the block size
    .equ BLOCK1, 1
    .equ BLOCK2, 5

!
! Multiply A by B and store result in C. Triple depth uTC version.
!
! %11 = N
!
    .text
    .globl main
main:
    clr      %5
	allocate %5, 0, 0, 0, 0
	
	set A, %1
	set B, %2
	set C, %3
	mov %11, %4

	!	create (fam1; 0; N-1;)
	setlimit %5, %11
	swch
	.ifdef BLOCK1
	setblock %5, BLOCK1
	.endif
	cred thread1, %5
	
	!	sync(fam1);
	mov %5, %0
	end


    ! %g0 = A 
    ! %g1 = B
    ! %g2 = C
    ! %g3 = N
    ! %l0 = i
    .align 64
	.registers 4 0 5  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread1:
    clr      %l4
	allocate %l4, 0, 0, 0, 0
	
	umul    %l0, %g3, %l0       ! %l0 = i*N
	sll     %l0,   2, %l0
	add     %l0, %g2, %l2       ! %l2 = &C[i*N]
	add     %l0, %g0, %l0       ! %l0 = &A[i*N]
	mov     %g1, %l1
	mov     %g3, %l3
	
	setlimit %l4, %g3
	swch
	.ifdef BLOCK2
	setblock %l4, BLOCK2
	.endif
	cred thread2, %l4
	mov %l4, %0
	end


    ! %g0 = &A[i*N]
    ! %g1 = B
    ! %g2 = &C[i*N]
    ! %g3 = N
    ! %l0 = j
    .align 64
	.registers 4 0 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread2:
	clr      %l4
    allocate %l4, 0, 0, 0, 0

    sll     %l0,   2, %l1
	add     %l1, %g2, %l5       ! %l5 = &C[i*N+j]
	add     %l1, %g1, %l1       ! %l1 = &B[j]
    mov     %g0, %l0            ! %l0 = &A[i*N]
	mov     %g3, %l2            ! %l2 = N
	clr     %l3                 ! %l3 = sum = 0

    setlimit %l4, %g3
    swch
    cred thread3, %l4
    
    mov     %l4, %0
    swch
    
	st      %l3, [%l5]         ! C[i*N+j] = sum
	end
	
	
	! %g0 = &A[i*N]
	! %g1 = &B[j]
	! %g2 = N
	! %s0 = sum
	! %l0 = k
    .align 64
	.registers 3 1 2  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread3:
	sll     %l0,   2, %l1
    add     %l1, %g0, %l1       ! %l1 = &A[i*N+k]
    ld      [%l1], %l1          ! %l1 =  A[i*N+k]
    
    umul    %l0, %g2, %l0       ! %l0 =  k*N
    sll     %l0,   2, %l0
    add     %l0, %g1, %l0       ! %l0 = &B[k*N+j]
    ld      [%l0], %l0          ! %l0 =  B[k*N+j]
    
    smul    %l1, %l0, %l0
    swch
    add     %d0, %l0, %s0
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

