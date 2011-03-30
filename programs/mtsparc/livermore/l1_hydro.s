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
    clr %4
    allocates %4            ! Start = 0, Step = 1
    setlimit %4, %11        ! Limit = N
    set      loop, %1
    crei     %1, %4

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
! %tg0        = X
! %tg1        = Y
! %tg2        = Z
! %tgf0,%tgf1 = Q
! %tgf2,%tgf3 = R
! %tgf4,%tgf5 = T
! %tl0        = i
!
    .align 64
    .registers 3 0 2  6 0 7    ! GR,SR,LR, GF,SF,LF
loop:
    sll     %tl0,   3, %tl1
    add     %tl1, %tg2, %tl1   ! %tl1 = &Z[i]
    ldd     [%tl1+80], %tlf3   ! %tlf3, %tlf4 = Z[i + 10]
    ldd     [%tl1+88], %tlf5   ! %tlf5, %tlf6 = Z[i + 11]
    sll     %tl0,   3, %tl1
    add     %tl1, %tg1, %tl1   ! %tl1 = &Y[i]
    ldd     [%tl1],    %tlf1   ! %tlf1,%tlf2 = Y[i]
    sll     %tl0,   3, %tl0
    add     %tl0, %tg0, %tl0   ! %tl0 = &X[i]

    fmuld   %tlf3, %tgf2, %tlf3; swch
    fmuld   %tlf5, %tgf4, %tlf5; swch
    faddd   %tlf3, %tlf5, %tlf3; swch
    fmuld   %tlf1, %tlf3, %tlf1; swch
    faddd   %tlf1, %tgf0, %tlf1; swch
    std     %tlf1, [%tl0]
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

