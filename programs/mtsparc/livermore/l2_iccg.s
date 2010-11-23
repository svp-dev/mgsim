/*
    Livermore kernel 2 -- ICCG (Incomplete Cholesky Conjugate Gradient)

    double x[1001], v[1001];

    int ipntp = 0;
    for (int m = log2(n); m >= 0; m--)
    {
        int ii   = 1 << m;
        int ipnt = ipntp;
        ipntp    = ipntp + ii;

        for (int i = 1; i < ii; i+=2)
        {
            int k = ipnt + i;
            x[ipntp + i / 2] = x[k] - (v[k]*x[k-1] + v[k+1]*x[k+1]);
        }
    }
*/
    .file "l2_iccq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:8\0"

    .text
    
!
! Main thread
!
! %11 = M (= log2 N)
!
    .global main
    .align 64
main:
    allocates %0, %5
    setstart %5, %11
    setlimit %5, -1
    setstep  %5, -1
    cred    outer, %5

    set     X, %1           ! %1 = X
    set     V, %2           ! %2 = V
    mov     1, %3           ! %3 = 1
    putg   %1, %5, 0
    putg   %2, %5, 1
    putg   %3, %5, 2
    puts   %0, %5, 0
    
    sync    %5, %1
    mov     %1, %0
    release %5
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
    .registers 3 1 3 0 0 0
outer:
    allocates %0, %l2
    sll      %g2, %l0, %l0       ! %l0 = ii
    setlimit %l2, %l0; swch
    setstart %l2, 1
    setstep  %l2, 2

    add      %d0, %l0, %l0; swch ! %l0 = ipntp + ii
    cred     inner, %l2

    putg     %g0, %l2, 0; swch   ! %g0 = X
    putg     %g1, %l2, 1         ! %g1 = V
    putg     %l0, %l2, 2         ! %g2 = ipntp
    putg     %d0, %l2, 3         ! %g3 = ipnt

    sync     %l2, %l1
    mov      %l1, %0; swch
    release  %l2
    
    mov      %l0, %s0            ! %s0 = ipntp + ii
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
    .registers 4 0 3 0 0 10
inner:
    add     %g3, %l0, %l2   ! %l2 = k = ipnt + i
    sll     %l2,   3, %l2
    add     %l2, %g1, %l1   ! %l1 = &V[k]
    ldd     [%l1], %lf2     ! %lf2,%lf3 = V[k]
    add     %l2, %g0, %l2   ! %l2 = &X[k]
    ldd     [%l2-8], %lf8   ! %lf8,%lf9 = X[k-1]
    ldd     [%l1+8], %lf4   ! %lf4,%lf5 = V[k+1]
    ldd     [%l2+8], %lf6   ! %lf6,%lf7 = X[k+1]
    ldd     [%l2+0], %lf0   ! %lf0,%lf1 = X[k]
    srl     %l0,   1, %l0
    add     %g2, %l0, %l0
    sll     %l0,   3, %l0
    add     %l0, %g0, %l0   ! %l0 = &X[ipntp + i / 2]
    fmuld   %lf2, %lf8, %lf2; swch
    fmuld   %lf4, %lf6, %lf4; swch
    faddd   %lf2, %lf4, %lf2; swch
    fsubd   %lf0, %lf2, %lf2; swch
    std     %lf2, [%l0]
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8    
    .align 64
V:  .skip 1001 * 8
