    .file "matmul1.s"

    ! Matrix width (only square matrices supported)
    .equ N, 10

!
! Multiply matrixA by matrixB and store result in matrixC. Single depth uTC version.
!
    .text
    .globl main
main:
	allocate %5, 0, 0, 0, 0

    set matrixA, %1
    set matrixB, %2
    set matrixC, %3
	set N, %4

	!	create (fam1; 0; N;)
	setlimit %5, N
	swch
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
	.registers 4 0 8 0 0 0      ! GR SR LR GF SF LF
	
	umul    %l0, %g3, %l0       ! %l0 = i*N
	sll     %l0,   2, %l0
	add     %l0, %g2, %l2       ! %l2 = &C[i*N]
	add     %l0, %g0, %l0       ! %l0 = &A[i*N]
	
    !
    ! for (int j = 0; j < N; j++) {
    !
	clr %l3                     ! %l3 = j
	jmp L1e
	swch
L1s:

	clr     %l5                 ! %l5 = sum = 0
	sll     %l3,   2, %l1
	add     %l1, %g1, %l1       ! %l1 = &B[j]

    !
	! for (int k = 0; k < N; k++) {
	!
    clr %l4                     ! %l4 = k
    jmp L2e
    swch
L2s:

    sll     %l4,   2, %l6
    add     %l6, %l0, %l6       ! %l6 = &A[i*N+k]
    ld      [%l6], %l6          ! %l6 =  A[i*N+k]
    sll     %g3,   2, %l7
    add     %l7, %l1, %l7       ! %l7 = &B[k*N+j]
    ld      [%l7], %l7          ! %l7 =  B[k*N+j]
    
    smul    %l6, %l7, %l6       ! %l6 = A[i*N+k] * B[k*N+j]
    swch
    
    add     %l5, %l6, %l5       ! %l5 = sum = sum + A[i*N+k] * B[k*N+j]
    
    !
    ! }
    !
    inc     %l4
L2e:cmp     %l4, %g3
	blt     L2s
	swch
	
	sll     %l3,   2, %l6
	add     %l6, %l2, %l6       ! %l6 = &C[i*N+j]
	st      %l5, [%l6]          ! C[i*N+j] = sum
    
    !
    ! }
    !
	inc     %l3
L1e:cmp     %l3, %g3
	blt     L1s
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

