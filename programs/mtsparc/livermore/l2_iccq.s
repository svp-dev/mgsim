!
! Livermore kernel 2 -- ICCG (Incomplete Cholesky Conjugate Gradient)
!
!    int ipntp = 0;
!    for (int m = M; m > 0; m--)
!    {
!        int ii   = 1 << m;
!        int ipnt = ipntp;
!        ipntp    = ipntp + ii;
!
!        for (int i = 1; i < ii; i+=2)
!        {
!            int k = ipnt + i;
!            x[ipntp + i / 2 + 1] = x[k] - (v[k]*x[k-1] + v[k+1]*x[k+1]);
!        }
!    }
!
    .file "l2_iccq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:8\0"

    .text
    
    .equ MAX_M, 16
    .equ MAX_N, 1 << 16

!
! Main thread
!
! %11 = M (= log2 N)
!
    .global main
    .align 64
main:
    set     X, %1           ! %1 = X
    set     V, %2           ! %2 = V
    mov     1, %3           ! %3 = 1
    clr     %4              ! %4 = 0
    
    allocate %5, 0, 0, 0, 0
    setstart %5, %11
    setlimit %5, 0
    setstep  %5, -1
    cred    outer, %5
    mov     %5, %0
    end

!
! Outer thread
!
! %g0 = X
! %g1 = V
! %g2 = 1
! %d0 = ipntp
! %l0 = m
!
    .globl outer
    .align 64
    .registers 3 1 5 0 0 0
outer:
    allocate %l4, 0, 0, 0, 0

    mov     %g1, %l1            ! %l1 = V
    mov     %d0, %l3            ! %l3 = ipnt
    sll     %g2, %l0, %l0       ! %l0 = ii
    add     %l0, 1, %l2
    setlimit %l4, %l2; swch
    setstart %l4, 1
    setstep  %l4, 2
    add     %d0, %l0, %l2; swch ! %l2 = ipntp + ii
    mov     %g0, %l0            ! %l0 = X
    
    cred    inner, %l4
    mov     %l4, %0; swch
    mov     %l2, %s0            ! %s0 = ipntp + ii
    end

!
! Inner thread
!
! %g0 = X
! %g1 = V
! %g2 = ipntp
! %g3 = ipnt
! %l0 = i
!
    .global inner
    .align 64
    .registers 4 0 7 0 0 0
inner:
    add     %g3, %l0, %l6   ! %l6 = k = ipnt + i
    sll     %l6,   2, %l5
    add     %l5, %g1, %l2   ! %l2 = &V[k]
    ld      [%l2], %l1      ! %l1 = V[k]
    add     %l5, %g0, %l5   ! %l5 = &X[k]
    ld      [%l5-4], %l4    ! %l4 = X[k-1]
    ld      [%l2+4], %l2    ! %l2 = V[k+1]
    ld      [%l5+4], %l3    ! %l3 = X[k+1]
    ld      [%l5+0], %l5    ! %l5 = X[k]
    srl     %l0,   1, %l0
    add     %g2, %l0, %l0
    sll     %l0,   2, %l0
    add     %l0, %g0, %l0   ! %l0 = &X[ipntp + i / 2]
    smul    %l1, %l4, %l1; swch
    smul    %l2, %l3, %l2; swch
    add     %l1, %l2, %l1
    sub     %l5, %l1, %l1; swch
    st      %l1, [%l0+4]
    end

    .section .bss
   
    .align 64
X:  .skip MAX_N * 2
    
    .align 64
V:  .skip MAX_N * 2
