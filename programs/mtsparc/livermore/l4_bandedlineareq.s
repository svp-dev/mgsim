/*
    Livermore kernel 4 -- Banded linear equations

    int m = (1001 - 7) / 2;
    double x[1001], y[1001];

    for (int i = 6; i < 1001; i += m)
    {
        double temp = 0;
        for(int j = 0; j < N/5; j++) {
            temp += x[i + j - 6] * y[j * 5 + 4];
        }

        x[i - 1] = y[4] * (x[i - 1] - temp);
    }
*/
    .file "l4_bandedlineareq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:256\0"

    .text
    
    .equ M, (1001 - 7) / 2
    
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
    
    clr      %4
    clr      %4
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
    .align 64
outer:
    .registers 3 0 5 0 0 6
    clr      %l3
    allocate %l3, 0, 0, 0, 0
    mov      %g0, %l1    ! %l1 = X
    mov      %g1, %l2    ! %l2 = Y
    fmovs    %f0, %lf0
    fmovs    %f0, %lf1   ! %lf0,%lf1 = temp = 0
    setlimit %l3, %g2; swch
    cred     inner, %l3
    
    sll     %l0, 3, %l4
    add     %l4, %g0, %l4    ! %l5 = &X[i]
    ldd     [%l4-8],  %lf2   ! %lf2,%lf3 = X[i-1]
    ldd     [%g1+32], %lf4   ! %lf4,%lf6 = Y[4]
    
    mov     %l3, %0; swch
    fsubd   %lf2, %lf0, %lf2; swch   ! %lf2,%lf3 = X[i-1] - temp
    fmuld   %lf4, %lf2, %lf4; swch
    std     %lf4, [%l4-8]
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
    .align 64
inner:
    .registers 3 0 2 0 2 4
    add     %g0, %l0, %l1   ! %l1 = i + j
    sll     %l1,   3, %l1
    add     %l1, %g1, %l1   ! %l1 = &X[i + j];
    ldd     [%l1-48], %lf2  ! %lf2,%lf3 = X[i + j - 6];
    
    sll     %l0,   2, %l1
    add     %l1, %l0, %l0   ! %l0 = j * 5;
    sll     %l0,   3, %l0
    add     %l0, %g2, %l0   ! %l0 = &Y[j * 5];
    ldd     [%l0+32], %lf0  ! %lf0,%lf1 = Y[j * 5 + 4];
    
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %df0, %lf0, %sf0
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
