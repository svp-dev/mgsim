/*
    Livermore kernel 6 -- General linear recurrence equations

    double x[1001], y[64][64];

    for(int i = 1; i < N; i++) {
        double sum = 0;
        for(int j = 0; j < i; j++) {
            sum += y[j][i] * x[i - j - 1];
        }
        x[i] = x[i] + sum;
    }
*/
    .file "l6_genlinreceq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:16\0"

    .text
    
!
! Main thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1           ! %1 = X
    set     Y, %2           ! %2 = Y
    clr     %3              ! %3 = token
    
    mov      2, %5      ! Local
    allocate %5, 0, 0, 0, 0
    setstart %5, 1
    setlimit %5, %11
    setblock %5, 2
    cred    outer, %5
    mov     %5, %0
    end
    
!
! Outer loop
!
! %g0  = X
! %g1  = Y
! %d0  = token
! %l0  = i
!
    .globl outer
    .align 64
outer:
    .registers 2 1 5 0 0 4
    
    clr      %l3
    allocate %l3, 0, 0, 0, 0

    sll     %l0,    3, %l1
    add     %l1,  %g1, %l1  ! %l1 = &Y[0][i]
    mov     %g0,  %l2       ! %l2 = X
    fmovs   %f0, %lf0
    fmovs   %f0, %lf1       ! %lf0,%lf1 = sum = 0
    
    setlimit %l3, %l0; swch
    mov     %d0, %0
    cred    inner, %l3
    sll     %l0,   3, %l4
    add     %l4, %g0, %l4   ! %l4 = &X[i]
    ldd     [%l4], %lf2
    mov     %l3, %0
    faddd   %lf2, %lf0, %lf0; swch  ! %lf0,%lf1 = X[i] + sum
    std     %lf0, [%l4]
    stbar
    clr     %s0
    end
    
!
! Inner loop
!
! %g0  = i
! %g1  = &Y[i]
! %g2  = X
! %df0,%df1 = sum
! %l0  = j
!
    .globl inner
    .align 64
inner:
    .registers 3 0 2 0 2 4
    sub     %g0,  %l0, %l1
    sll     %l1,    3, %l1
    add     %l1,  %g2, %l1
    ldd     [%l1-8], %lf0       ! %lf0,%lf1 = X[i - j - 1]
    
    sll     %l0,   9, %l0
    add     %l0, %g1, %l0
    ldd     [%l0], %lf2         ! %lf2,%lf3 = Y[i][j]
    
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %lf0, %df0, %sf0
    end
    
    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 64 * 64 * 8
