    .file "matmul1.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    ! Maximum matrix width (only square matrices supported)
    .equ MAX_N, 16

!
! Multiply A by B and store result in C. Single depth uTC version.
!
! %11 = N
!
    .text
    .globl main
main:
	allocates %0, %5            ! Default

	!	create (fam1; 0; N;)
	setlimit %5, %11
	swch
	cred thread1, %5
	
    set     A, %1
    set     B, %2
    set     C, %3
    putg    %1,  %5, 0
    putg    %2,  %5, 1
    putg    %3,  %5, 2
    putg    %11, %5, 3

	!	sync(fam1);
	sync    %5, %1
	mov     %1, %0
	release %5
	end


    ! %tg0 = A 
    ! %tg1 = B
    ! %tg2 = C
    ! %tg3 = N
    ! %tl0 = i
    .align 64
	.registers 4 0 8 0 0 0      ! GR SR LR GF SF LF	
thread1:
	umul    %tl0, %tg3, %tl0       ! %tl0 = i*N
	sll     %tl0,   2, %tl0
	add     %tl0, %tg2, %tl2       ! %tl2 = &C[i*N]
	add     %tl0, %tg0, %tl0       ! %tl0 = &A[i*N]
	
    !
    ! for (int j = 0; j < N; j++) {
    !
	clr %tl3                     ! %tl3 = j
	ba L1e
	swch
L1s:

	clr     %tl5                 ! %tl5 = sum = 0
	sll     %tl3,   2, %tl1
	add     %tl1, %tg1, %tl1       ! %tl1 = &B[j]

    !
	! for (int k = 0; k < N; k++) {
	!
    clr %tl4                     ! %tl4 = k
    ba L2e
    swch
L2s:

    sll     %tl4,   2, %tl6
    add     %tl6, %tl0, %tl6       ! %tl6 = &A[i*N+k]
    ld      [%tl6], %tl6          ! %tl6 =  A[i*N+k]
    sll     %tg3,   2, %tl7
    add     %tl7, %tl1, %tl7       ! %tl7 = &B[k*N+j]
    ld      [%tl7], %tl7          ! %tl7 =  B[k*N+j]
    
    smul    %tl6, %tl7, %tl6       ! %tl6 = A[i*N+k] * B[k*N+j]
    swch
    
    add     %tl5, %tl6, %tl5       ! %tl5 = sum = sum + A[i*N+k] * B[k*N+j]
    
    !
    ! }
    !
    inc     %tl4
L2e:cmp     %tl4, %tg3
	blt     L2s
	swch
	
	sll     %tl3,   2, %tl6
	add     %tl6, %tl2, %tl6       ! %tl6 = &C[i*N+j]
	st      %tl5, [%tl6]          ! C[i*N+j] = sum
    
    !
    ! }
    !
	inc     %tl3
L1e:cmp     %tl3, %tg3
	blt     L1s
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

