/*
    Livermore kernel 1 -- Hydro fragment

    double X[1001], Y[1001], Z[1001];

    for (k=0 ; k < N; k++)
    {
        x[k] = Q + Y[k] * (R * Z[k+10] + T * Z[k+11]);
    }
*/
    .file "l1_hydro.s"

!
! Constants
!
    .section .rodata    
    Q: .double 100
    R: .double 5
    T: .double 2

    .text
!
! Main thread
!
! %11 = N (0 <= N < 990)
!
    .globl main
main:
    allocates %0, %4         ! Start = 0, Step = 1
    setlimit %4, %11        ! Limit = N
    cred     loop, %4

    set     Q, %1; ldd [%1], %f2
    set     R, %1; ldd [%1], %f4
    set     T, %1; ldd [%1], %f6
    set     X, %1
    set     Y, %2
    set     Z, %3
    putg    %1, %4, 0
    putg    %2, %4, 1
    putg    %3, %4, 2
    fputg   %f2, %4, 0
    fputg   %f3, %4, 1
    fputg   %f4, %4, 2
    fputg   %f5, %4, 3
    fputg   %f6, %4, 4
    fputg   %f7, %4, 5

    sync     %4, %1
    mov      %1, %0
    release  %4
    end

!
! Loop thread:
! x[i] = Q + Y[i] * (R * Z[i + 10] + T * Z[i + 11]);
!
! %g0       = X
! %g1       = Y
! %g2       = Z
! %gf0,%gf1 = Q
! %gf2,%gf3 = R
! %gf4,%gf5 = T
! %l0       = i
!
    .align 64
    .registers 3 0 2  6 0 6     ! GR,SR,LR, GF,SF,LF
loop:
    sll     %l0,   3, %l1
    add     %l1, %g2, %l1   ! %l1 = &Z[i]
    ldd     [%l1+80], %lf2  ! %lf2, %lf3 = Z[i + 10]
    ldd     [%l1+88], %lf4  ! %lf4, %lf5 = Z[i + 11]
    sll     %l0,   3, %l1
    add     %l1, %g1, %l1   ! %l1 = &Y[i]
    ldd     [%l1],    %lf0  ! %lf0,%lf1 = Y[i]
    sll     %l0,   3, %l0
    add     %l0, %g0, %l0   ! %l0 = &X[i]

    fmuld   %lf2, %gf2, %lf2; swch
    fmuld   %lf4, %gf4, %lf4; swch
    faddd   %lf2, %lf4, %lf2; swch
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %lf0, %gf0, %lf0; swch
    std     %lf0, [%l0]
    end

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:128\0"

!
! Data
!
    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
    .align 64
Z:  .skip 1001 * 8

