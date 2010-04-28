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
    allocate 2, %5      ! Local
    setstart %5, 1
    setlimit %5, %11
    setblock %5, 2
    cred    outer, %5

    set     X, %1           ! %1 = X
    set     Y, %2           ! %2 = Y
    putg    %1, %5, 0
    putg    %2, %5, 1
    puts    %0, %5, 0       ! %3 = token
    
    sync    %5, %1
    release %5
    mov     %1, %0
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
    .registers 2 1 5 0 0 4    
outer:
    allocate %0, %l3

    sll     %l0,    3, %l1
    add     %l1,  %g1, %l1  ! %l1 = &Y[0][i]
    
    setlimit %l3, %l0; swch
    mov     %d0, %0
    cred    inner, %l3

    putg    %l0, %l3, 0
    putg    %l1, %l3, 1
    putg    %g0, %l3, 2     ! %g2 = X
    fputs   %f0, %l3, 0
    fputs   %f0, %l3, 1     ! %lf0,%lf1 = sum = 0

    sll     %l0,   3, %l4
    add     %l4, %g0, %l4   ! %l4 = &X[i]
    ldd     [%l4], %lf2
    
    sync    %l3, %l0
    mov     %l0, %0
    fgets   %l3, 0, %lf0
    fgets   %l3, 1, %lf1
    release %l3
    
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
    .registers 3 0 2 0 2 4
inner:
    sub     %g0,  %l0, %l1
    sll     %l1,    3, %l1
    add     %l1,  %g2, %l1
    ldd     [%l1-8], %lf0       ! %lf0,%lf1 = X[i - j - 1]
    
    sll     %l0,   9, %l0
    add     %l0, %g1, %l0
    ldd     [%l0], %lf2         ! %lf2,%lf3 = Y[i][j]
    
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %lf0, %df0, %lf0; swch
    fmovs   %lf0, %sf0; swch
    fmovs   %lf1, %sf1
    end
    
    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 64 * 64 * 8
