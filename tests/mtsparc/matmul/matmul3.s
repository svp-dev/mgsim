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
    clr %5
	allocates %5             ! Default
	
	!	create (fam1; 0; N-1;)
	setlimit %5, %11
	swch
	.ifdef BLOCK1
	setblock %5, BLOCK1
	.endif
	set thread1, %1
	crei %1, %5
	
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
    clr %tl4
	allocates %tl4            ! Default
	setlimit %tl4, %tg3
	swch
	.ifdef BLOCK2
	setblock %tl4, BLOCK2
	.endif
	set thread2, %tl2
	crei %tl2, %tl4
	
	umul    %tl0, %tg3, %tl0       ! %tl0 = i*N
	sll     %tl0,   2, %tl0
	add     %tl0, %tg2, %tl2       ! %tl2 = &C[i*N]
	add     %tl0, %tg0, %tl0       ! %tl0 = &A[i*N]
	putg    %tl0, %tl4, 0
	putg    %tg1, %tl4, 1
	putg    %tl2, %tl4, 2
	putg    %tg3, %tl4, 3
	
	sync    %tl4, %tl0
	release %tl4
	mov     %tl0, %0
	end


    ! %tg0 = &A[i*N]
    ! %tg1 = B
    ! %tg2 = &C[i*N]
    ! %tg3 = N
    ! %tl0 = j
    .align 64
	.registers 4 0 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread2:
    mov 1, %tl4 ! local place
    allocates %tl4
    setlimit %tl4, %tg3
    swch
    set thread3, %tl1
    crei %tl1, %tl4
   
    sll     %tl0,   2, %tl1
	add     %tl1, %tg2, %tl5       ! %tl5 = &C[i*N+j]
	add     %tl1, %tg1, %tl1       ! %tg1 = &B[j]
    putg    %tg0, %tl4, 0         ! %tg0 = &A[i*N]
    putg    %tl1, %tl4, 1         ! %tg1 = &B[j]
	putg    %tg3, %tl4, 2         ! %tg2 = N
	
	puts    %0, %tl4, 0          ! %td0 = sum = 0

    sync    %tl4, %tl0
    mov     %tl0, %0
    swch
    gets    %tl4, 0, %tl3
    release %tl4
    
	st      %tl3, [%tl5]         ! C[i*N+j] = sum
	end
	
	
	! %tg0 = &A[i*N]
	! %tg1 = &B[j]
	! %tg2 = N
	! %ts0 = sum
	! %tl0 = k
    .align 64
	.registers 3 1 2  0 0 0	    ! GR,SR,LR, GF,SF,LF
thread3:
	sll     %tl0,   2, %tl1
    add     %tl1, %tg0, %tl1       ! %tl1 = &A[i*N+k]
    ld      [%tl1], %tl1          ! %tl1 =  A[i*N+k]
    
    umul    %tl0, %tg2, %tl0       ! %tl0 =  k*N
    sll     %tl0,   2, %tl0
    add     %tl0, %tg1, %tl0       ! %tl0 = &B[k*N+j]
    ld      [%tl0], %tl0          ! %tl0 =  B[k*N+j]
    
    smul    %tl1, %tl0, %tl0
    swch
    add     %td0, %tl0, %ts0
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

