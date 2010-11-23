! Livermore kernel 7 -- Equation of state fragment
!
!    int u[N + U], x[N], y[N], z[N];
!
!    for(int i = 0; i < N; i++) {
!        x[i] = u[i+0] + R * (z[i+0] + R * y[i+0]) +
!          T * (u[i+3] + R * (u[i+2] + R * u[i+1]) +
!          T * (u[i+6] + Q * (u[i+5] + Q * u[i+4])));
!    }
!
    .file "l7_eqofstatefrag.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"

    .text
    
    .equ MAX_N, 1048576
    .equ Q, 10
    .equ R, 20
    .equ T, 40

!
! Main thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    allocates %0, %5
    setlimit %5, %11
    cred loop, %5

    set     X, %1      ! %1 = X
    set     Y, %2      ! %2 = Y
    set     Z, %3      ! %3 = Z
    set     U, %4      ! %4 = U
    putg    %1, %5, 0
    putg    %2, %5, 1
    putg    %3, %5, 2
    putg    %4, %5, 3
    
    sync %5, %1
    mov %1, %0
    release %5
    end

!
! Loop thread
! %g0 = X
! %g1 = Y
! %g2 = Z
! %g3 = U
! %l0 = i
!
    .globl loop
    .align 64
    .registers 4 0 10 0 0 0
loop:
    sll     %l0,   2, %l0
    add     %l0, %g3, %l1   ! %l1 = &u[i]
    ld      [%l1+16], %l2   ! %l2 = u[i+4]
    ld      [%l1+20], %l3   ! %l3 = u[i+5]   
    ld      [%l1+24], %l4   ! %l4 = u[i+6]
    ld      [%l1+ 4], %l5   ! %l5 = u[i+1]
    ld      [%l1+ 8], %l6   ! %l6 = u[i+2]
    ld      [%l1+12], %l7   ! %l7 = u[i+3]
    add     %l0, %g1, %l8
    ld      [%l8], %l8      ! %l8 = y[i]
    add     %l0, %g2, %l9
    ld      [%l9], %l9      ! %l9 = z[i]
    add     %l0, %g0, %l0   ! %l0 = &x[i]
    ld      [%l1], %l1      ! %l1 = u[i+0]
    
    smul    %l2,   Q, %l2; swch
    add     %l2, %l3, %l2; swch
    smul    %l2,   Q, %l2
    add     %l2, %l4, %l2; swch
    smul    %l2,   T, %l2
    
    smul    %l5,   R, %l5; swch
    add     %l5, %l6, %l5; swch
    smul    %l5,   R, %l5
    add     %l5, %l2, %l2
    add     %l2, %l7, %l2; swch
    smul    %l2,   T, %l2
    
    smul    %l8,   R, %l8; swch
    add     %l8, %l9, %l8; swch
    smul    %l8,   R, %l8
    add     %l8, %l2, %l2
    add     %l2, %l1, %l1; swch
    
    st      %l1, [%l0]
    end

    .section .bss
    .align 64
X:  .skip MAX_N * 4
Y:  .skip MAX_N * 4
Z:  .skip MAX_N * 4
U:  .skip (MAX_N + 6) * 4
