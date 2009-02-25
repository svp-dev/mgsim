! Livermore kernel 4 -- Banded linear equations
!
!    int m = (1001 - 7) / 2;
!
!    int x[N], Y[N];
!
!    for (int i = 6; i < 1001; i += m)
!    {
!        int temp = 0;
!        for(int j = 0; j < N/5; j++) {
!            temp += x[i + j - 6] * y[j * 5 + 4];
!        }
!
!        x[i - 1] = y[4] * (x[i - 1] - temp);
!    }
!
    .file "l4_bandedlineareq.s"
    .text
    
    .equ MAX_N, 1048576
    .equ M,     (1001 - 7) / 2
    
!
! Main thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1       ! %1 = X
    set     Y, %2       ! %2 = Y   
    udiv    %11, 5, %3  ! %3 = N / 5
    
    set     M, %5       ! %5 = M
    set     1001, %6    ! %6 = 1001
    
    allocate %4, 0, 0, 0, 0
    setstart %4, 6
    setlimit %4, %6
    setstep  %4, %5
    cred    outer, %4
    mov     %4, %0
    end
    
!
! Outer loop thread
!
! %g0 = X
! %g1 = Y
! %g2 = N/5
! %l0 = i
!
    .globl outer
    .align 64
outer:
    .registers 3 0 8 0 0 0
    allocate %l4, 0, 0, 0, 0
    mov     %g0, %l1    ! %l1 = X
    mov     %g1, %l2    ! %l2 = Y
    clr     %l3         ! %l3 = temp = 0
    setlimit %l4, %g2; swch
    cred    inner, %l4
    
    sll     %l0, 2, %l5
    add     %l5, %g0, %l5    ! %l5 = &X[i]
    ld      [%l5-4],  %l6    ! %l6 = X[i-1]
    ld      [%g1+16], %l7    ! %l7 = Y[4]
    
    mov     %l4, %0; swch
    sub     %l6, %l3, %l6; swch   ! %l6 = X[i-1] - temp
    smul    %l7, %l6, %l7; swch
    st      %l7, [%l5-4]
    end

!
! Inner loop thread
!
! %g0 = i
! %g1 = X
! %g2 = Y
! %d0 = temp
! %l0 = j
!
    .globl inner
    .align 64
inner:
    .registers 3 1 3 0 0 0
    add     %g0, %l0, %l1   ! %l1 = i + j
    sll     %l1,   2, %l1
    add     %l1, %g1, %l1   ! %l1 = &X[i + j];
    ld      [%l1-24], %l1   ! %l1 = X[i + j - 6];
    
    sll     %l0,   4, %l2
    add     %l2, %l0, %l0   ! %l0 = j * 5;
    sll     %l0,   2, %l0
    add     %l0, %g2, %l0   ! %l0 = &Y[j * 5];
    ld      [%l0+16], %l0   ! %l0 = Y[j * 5 + 4];
    
    smul    %l0, %l1, %l0; swch
    add     %d0, %l0, %s0
    end


    .section .bss
    .align 64
X:  .skip MAX_N * 4

    .align 64
Y:  .skip MAX_N * 4
