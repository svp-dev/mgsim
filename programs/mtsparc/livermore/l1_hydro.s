!
! Livermore kernel 1 -- Hydro fragment
!
    .file "l1_hydro.s"
    .text

!
! Constants
!
    .equ MAX_N, 1048576
    
    .equ Q, 100
    .equ R, 5
    .equ T, 2

!
! Main thread
!
! %11 = N
!
    .globl main
main:
    set     X, %1
    set     Y, %2
    set     Z, %3

    allocate %4, 0, 0, 0, 0 ! Start = 0, Step = 1
    setlimit %4, %11        ! Limit = N
    cred     loop, %4
    mov      %4, %0         ! Sync
    end

!
! Loop thread:
! x[i] = Q + Y[i] * (R * Z[i + 10] + T * Z[i + 11]);
!
! %g0 = X
! %g1 = Y
! %g2 = Z
! %l0 = i
!
    .globl loop
    .align 64
loop:
    .registers 3 0 4  0 0 0     ! GR,SR,LR, GF,SF,LF

    sll     %l0,   2, %l3
    add     %l3, %g2, %l3   ! %l3 = &Z[i]
    ld      [%l3+40], %l2   ! %l2 = Z[i + 10]
    ld      [%l3+44], %l3   ! %l3 = Z[i + 11]
    sll     %l0,   2, %l1
    add     %l1, %g1, %l1   ! %l1 = &Y[i]
    ld      [%l1+0],  %l1   ! %l1 = Y[i]
    sll     %l0,   2, %l0
    add     %l0, %g0, %l0   ! %l0 = &X[i]

    smul    %l2, R, %l2; swch
    smul    %l3, T, %l3; swch
    add     %l2, %l3, %l2
    smul    %l1, %l2, %l1; swch
    add     %l1, Q, %l1
    st      %l1, [%l0+0]

    end

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"

!
! Data
!
    .section .bss
    .align 64
X:  .skip MAX_N*4

    .align 64
Y:  .skip MAX_N*4

    .align 64
Z:  .skip (MAX_N + 11)*4

