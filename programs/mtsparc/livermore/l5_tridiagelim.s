! Livermore kernel 5 -- Tri-diagonal elimination, below diagonal
!
!    int x[N], y[N], z[N];
!
!    for(int i = 1; i < N; i++) {
!        x[i] = z[i] * (y[i] - x[i - 1]);
!    }
!
    .file "l5_tridiagelim.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"

    .text
    
    .equ MAX_N, 1048576
    
!
! Main Thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1       ! %1 = X
    set     Y, %2       ! %2 = Y
    set     Z, %3       ! %3 = Z
    
    ld      [%1], %4    ! %4 = X[0]
    
    allocate %5, 0, 0, 0, 0
    setlimit %5, %11
    mov     %4, %0      ! Wait for memory
    cred    loop, %5
    mov     %5, %0
    end

!    
! Loop thread
!
! %g0 = X
! %g1 = Y
! %g2 = Z
! %d0 = X[i-1]
! %l0 = i
    .globl loop
    .align 64
loop:
    .registers 3 1 3 0 0 0
    sll     %l0,   2, %l0
    add     %l0, %g1, %l1
    ld      [%l1], %l1          ! %l1 = Y[i]
    add     %l0, %g2, %l2
    ld      [%l2], %l2          ! %l2 = Z[i]
    add     %l0, %g0, %l0       ! %l0 = &X[i]
    sub     %l1, %d0, %l1; swch ! %l1 = Y[i] - X[i-1]
    smul    %l2, %l1, %s0; swch ! %s0 = X[i]
    st      %s0, [%l0]
    end

    .section .bss
    .align 64
X:  .skip MAX_N * 4
    .align 64
Y:  .skip MAX_N * 4
    .align 64
Z:  .skip MAX_N * 4
    
