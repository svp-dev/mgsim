! Livermore kernel 6 -- General linear recurrence equations
!
!    double x[N], y[N][N];
!
!    for(int i = 1; i < N; i++) {
!        double sum = 0.01;
!        for(int j = 0; j < i; j++) {
!            sum += y[i][j] * x[i - j + 1];
!        }
!        x[i] = sum;
!    }
!
    .file "l6_genlinreceq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:256\0"

    .text
    
    .equ MAX_N, 1024
    
!
! Main thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1           ! %0 = X
    set     Y, %2           ! %1 = Y
    set     MAX_N, %3       ! %2 = MAX_N
    clr     %4              ! %3 = token
    
    ! Load 0.01 into %f2
    set initial, %5
    ldd [%5], %f2
    
    allocate %5, 0, 0, 1, 1
    setstart %5, 1
    setlimit %5, %11
    setblock %5, 2
    setplace %5, 0
    fmovs   %f2, %f0
    cred    outer, %5
    mov     %5, %0
    end
    
!
! Outer loop
!
! %g0  = X
! %g1  = Y
! %g2  = MAX_N
! %d0  = token
! %l0  = i
! %gf0, %gf1 = 0.01
!
    .globl outer
    .align 64
outer:
    .registers 3 1 5 2 0 2
    
    allocate %l3, 0, 0, 0, 0

    umul    %l0,  %g2, %l1
    sll     %l1,    3, %l1
    add     %l1,  %g1, %l1  ! %l1 = &Y[i]
    mov     %g0,  %l2       ! %l2 = X
    fmovs   %gf0, %lf0
    fmovs   %gf1, %lf1      ! %lf0,%lf1 = 0.01
    
    setlimit %l3, %l0; swch
    mov     %d0, %0
    cred    inner, %l3
    sll     %l0,   3, %l4
    add     %l4, %g0, %l4   ! %l4 = &X[i]
    mov     %l3, %0
    std     %lf0, [%l4]
    stbar
    mov     %0, %s0
    end
    
!
! Inner loop
!
! %g0  = i
! %g1  = &Y[i]
! %g2  = X
! %df0 = sum
! %l0  = j
!
    .globl inner
    .align 64
inner:
    .registers 3 0 2 0 2 4
    sub     %g0,  %l0, %l1
    sll     %l1,    3, %l1
    add     %l1,  %g2, %l1
    ldd     [%l1+8], %lf0       ! %lf0 = X[i - j + 1]
    
    sll     %l0,   3, %l0
    add     %l0, %g1, %l0
    ldd     [%l0], %lf2         ! %lf1 = Y[i][j]
    
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %lf0, %df0, %lf0; swch
    fmovs   %lf0, %sf0
    fmovs   %lf1, %sf1
    end
    
    .data
    .align 8
initial:
    .double 0.01
    
    
    .section .bss
    .align 64
X:  .skip MAX_N * 8
    .align 64
Y:  .skip MAX_N * MAX_N * 8
