    .file "matmul3.s"

    ! Matrix width (only square matrices supported)
    .equ N,         10
    
    ! Block sizes, comment lines to not set the block size
    .equ BLOCK1,    1
    .equ BLOCK2,    5

!
! Multiply matrixA by matrixB and store result in matrixC. Triple depth uTC version.
!
    .text
    .globl main
main:
	allocate %5, 0, 0, 0, 0
	
	set matrixA, %1
	set matrixB, %2
	set matrixC, %3
	set N,       %4

	!	create (fam1; 0; N-1;)
	setlimit %5, N
	swch
	.ifdef BLOCK1
	setblock %5, BLOCK1
	.endif
	cred thread1, %5
	
	!	sync(fam1);
	mov %5, %0
	end


    ! %g0 = matrixA 
    ! %g1 = matrixB
    ! %g2 = matrixC
    ! %g3 = N
    ! %l0 = i
    .align 64
thread1:
	.registers 4 0 5  0 0 0	    ! GR,SR,LR, GF,SF,LF

	allocate %l4, 0, 0, 0, 0
	
	umul    %l0, %g3, %l0       ! %l0 = i*N
	sll     %l0,   2, %l0
	add     %l0, %g2, %l2       ! %l2 = &C[i*N]
	add     %l0, %g0, %l0       ! %l0 = &A[i*N]
	mov     %g1, %l1
	mov     %g3, %l3
	
	setlimit %l4, N
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
thread2:
	.registers 4 0 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
    allocate %l4, 0, 0, 0, 0

    sll     %l0,   2, %l1
	add     %l1, %g2, %l5       ! %l5 = &C[i*N+j]
	add     %l1, %g1, %l1       ! %l1 = &B[j]
    mov     %g0, %l0            ! %l0 = &A[i*N]
	mov     %g3, %l2            ! %l2 = N
	clr     %l3                 ! %l3 = sum = 0

    setlimit %l4, N
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
thread3:
	.registers 3 1 2  0 0 0	    ! GR,SR,LR, GF,SF,LF
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
    .globl matrixC
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
    .int 3
    .endr

