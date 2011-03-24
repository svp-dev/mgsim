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
    
	allocates %0, %5            ! Default
	
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


    ! %tg0 = A 
    ! %tg1 = B
    ! %tg2 = C
    ! %tg3 = N
    ! %tl0 = i
    .align 64
	.registers 4 0 5  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread1:
	allocates %tl0, %tl4          ! Default
	setlimit %tl4, %tg3
	swch
	cred thread2, %tl4
	
	umul    %tl0, %tg3, %tl0       ! %tl0 = i*N
	sll     %tl0,   2, %tl0
	add     %tl0, %tg2, %tl2       ! %tl2 = &C[i*N]
	add     %tl0, %tg0, %tl0       ! %tl0 = &A[i*N]
	putg    %tl0, %tl4, 0
	putg    %tg1, %tl4, 1
	putg    %tl2, %tl4, 2
	putg    %tg3, %tl4, 3
	
	sync %tl4, %tl0
	release %tl4
	mov %tl0, %0
	end


    ! %tg0 = &A[i*N]
    ! %tg1 = B
    ! %tg2 = &C[i*N]
    ! %tg3 = N
    ! %tl0 = j
    .align 64
	.registers 4 0 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread2:
    sll     %tl0,   2, %tl1
	add     %tl1, %tg1, %tl1       ! %tl1 = &B[j]
	clr     %tl2                 ! %tl2 = sum = 0

    !
	! for (int k = 0; k < N; k++) {
	!
    clr %tl3                     ! %tl3 = k
    ba L2e
    swch
L2s:
    sll     %tl3,   2, %tl4
    add     %tl4, %tg0, %tl4       ! %tl4 = &A[i*N+k]
    ld      [%tl4], %tl4          ! %tl4 =  A[i*N+k]
    sll     %tg3,   2, %tl5
    add     %tl5, %tl1, %tl5       ! %tl5 = &B[k*N+j]
    ld      [%tl5], %tl5          ! %tl5 =  B[k*N+j]
    
    smul    %tl4, %tl5, %tl4       ! %tl4 = A[i*N+k] * B[k*N+j]
    swch
    
    add     %tl2, %tl4, %tl2       ! %tl2 = sum = sum + A[i*N+k] * B[k*N+j]
    
    !
    ! }
    !
    inc     %tl3
L2e:cmp     %tl3, %tg3
	blt     L2s
	swch
	
	sll     %tl0,   2, %tl0
	add     %tl0, %tg2, %tl0       ! %tl0 = &C[i*N+j]
	st      %tl2, [%tl0]          ! C[i*N+j] = sum
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

