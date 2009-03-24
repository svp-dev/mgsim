! Livermore kernel 3 -- Inner product
!
    .file "l3_innerprod.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"

    .text
    
    .equ MAX_N, 1048576

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
    clr     %3              ! %3 = sum = 0
    
    allocate %4, 0, 0, 0, 0
    setlimit %4, %11
    cred    loop, %4

    mov     %4, %0
    end

!
! Loop thread
!
! %g0 = X
! %g1 = Y
! %d0 = sum
! %l0 = i
    .globl loop
    .align 64
loop:
    .registers 2 1 3 0 0 0
    sll     %l0, 2, %l0
    add     %l0, %g1, %l1
    ld      [%l1], %l1
    add     %l0, %g0, %l0
    ld      [%l0], %l0
    smul    %l0, %l1, %l0; swch
    add     %d0, %l0, %s0
    end

    .section .bss

    .align 64
X:  .skip MAX_N * 4

    .align 64
Y:  .skip MAX_N * 4
