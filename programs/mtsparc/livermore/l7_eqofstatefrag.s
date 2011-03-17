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
    allocateng %0, %5
    setlimitng %5, %11
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
! %tg0 = X
! %tg1 = Y
! %tg2 = Z
! %tg3 = U
! %tl0 = i
!
    .globl loop
    .align 64
    .registers 4 0 10 0 0 0
loop:
    sll     %tl0,   2, %tl0
    add     %tl0, %tg3, %tl1   ! %tl1 = &u[i]
    ld      [%tl1+16], %tl2   ! %tl2 = u[i+4]
    ld      [%tl1+20], %tl3   ! %tl3 = u[i+5]   
    ld      [%tl1+24], %tl4   ! %tl4 = u[i+6]
    ld      [%tl1+ 4], %tl5   ! %tl5 = u[i+1]
    ld      [%tl1+ 8], %tl6   ! %tl6 = u[i+2]
    ld      [%tl1+12], %tl7   ! %tl7 = u[i+3]
    add     %tl0, %tg1, %tl8
    ld      [%tl8], %tl8      ! %tl8 = y[i]
    add     %tl0, %tg2, %tl9
    ld      [%tl9], %tl9      ! %tl9 = z[i]
    add     %tl0, %tg0, %tl0   ! %tl0 = &x[i]
    ld      [%tl1], %tl1      ! %tl1 = u[i+0]
    
    smul    %tl2,   Q, %tl2; swch
    add     %tl2, %tl3, %tl2; swch
    smul    %tl2,   Q, %tl2
    add     %tl2, %tl4, %tl2; swch
    smul    %tl2,   T, %tl2
    
    smul    %tl5,   R, %tl5; swch
    add     %tl5, %tl6, %tl5; swch
    smul    %tl5,   R, %tl5
    add     %tl5, %tl2, %tl2
    add     %tl2, %tl7, %tl2; swch
    smul    %tl2,   T, %tl2
    
    smul    %tl8,   R, %tl8; swch
    add     %tl8, %tl9, %tl8; swch
    smul    %tl8,   R, %tl8
    add     %tl8, %tl2, %tl2
    add     %tl2, %tl1, %tl1; swch
    
    st      %tl1, [%tl0]
    end

    .section .bss
    .align 64
X:  .skip MAX_N * 4
Y:  .skip MAX_N * 4
Z:  .skip MAX_N * 4
U:  .skip (MAX_N + 6) * 4
