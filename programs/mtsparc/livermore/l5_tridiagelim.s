/*
    Livermore kernel 5 -- Tri-diagonal elimination, below diagonal

    double x[1001], y[1001], z[1001];

    for(int i = 1; i < N; i++)
    {
        x[i] = z[i] * (y[i] - x[i - 1]);
    }
*/
    .file "l5_tridiagelim.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1001\0"

    .text
    
!
! Main Thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1       ! %1 = X
    set     Y, %2       ! %2 = Y
    set     Z, %3       ! %3 = Z
    
    ldd     [%1], %f2   ! %lf0,%lf1 = X[0]
    
    clr      %5
    allocate %5, 0, 0, 0, 1
    setstart %5, 1
    setlimit %5, %11
    cred    loop, %5
    mov     %5, %0
    end

!    
! Loop thread
!
! %g0 = X
! %g1 = Y
! %g2 = Z
! %d0 = X[i-1]
! %l0 = i
    .align 64
    .registers 3 0 2 0 2 4
loop:
    sll     %l0,   3, %l0
    add     %l0, %g1, %l1
    ldd     [%l1], %lf0             ! %lf0,%lf1 = Y[i]
    add     %l0, %g2, %l1
    ldd     [%l1], %lf2             ! %lf2,%lf3 = Z[i]
    add     %l0, %g0, %l1           ! %l1 = &X[i]
    fsubd   %lf0, %df0, %lf0; swch  ! %lf0,%lf1 = Y[i] - X[i-1]
    fmuld   %lf2, %lf0, %sf0; swch  ! %s0 = X[i]
    std     %sf0, [%l1]
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
    .align 64
Z:  .skip 1001 * 8
    
