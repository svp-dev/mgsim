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
    allocate %0, %5
    setstart %5, 1
    setlimit %5, %11
    cred    loop, %5

    set     X, %1       ! %1 = X
    ldd     [%1], %f2   ! %f2,%f3 = X[0]
    set     Y, %2       ! %2 = Y
    set     Z, %3       ! %3 = Z
    
    putg    %1, %5, 0
    putg    %2, %5, 1
    putg    %3, %5, 2
    fputs   %f2, %5, 0
    fputs   %f3, %5, 1

    sync    %5, %1
    release %5
    mov     %1, %0
    end

!    
! Loop thread
!
! %g0 = X
! %g1 = Y
! %g2 = Z
! %df0,df1 = X[i-1]
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
    fmuld   %lf2, %lf0, %lf0; swch  ! %lf0,%lf1 = X[i]
    std     %lf0, [%l1]; swch
    fmovs   %lf0, %sf0
    fmovs   %lf1, %sf1
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
    .align 64
Z:  .skip 1001 * 8
    
