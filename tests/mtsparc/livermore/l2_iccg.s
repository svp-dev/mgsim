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
    clr %5
    allocates %5
    setstart %5, %11
    setlimit %5, -1
    setstep  %5, -1
    set     outer, %1
    crei    %1, %5

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
! %tg0 = X
! %tg1 = V
! %tg2 = 1
! %td0 = ipntp
! %tl0 = m
!
    .globl outer
    .align 64
    .registers 3 1 3 0 0 0
outer:
    clr %tl2
    allocates %tl2
    sll      %tg2, %tl0, %tl0       ! %tl0 = ii
    setlimit %tl2, %tl0; swch
    setstart %tl2, 1
    setstep  %tl2, 2

    add      %td0, %tl0, %tl0; swch ! %tl0 = ipntp + ii
    set      inner, %tl1
    crei     %tl1, %tl2

    putg     %tg0, %tl2, 0; swch   ! %tg0 = X
    putg     %tg1, %tl2, 1         ! %tg1 = V
    putg     %tl0, %tl2, 2         ! %tg2 = ipntp
    putg     %td0, %tl2, 3         ! %tg3 = ipnt

    sync     %tl2, %tl1
    mov      %tl1, %0; swch
    release  %tl2
    
    mov      %tl0, %ts0            ! %ts0 = ipntp + ii
    end

!
! Inner thread
!
! %tg0 = X
! %tg1 = V
! %tg2 = ipntp
! %tg3 = ipnt
! %tl0 = i
!
    .global inner
    .align 64
    .registers 4 0 3 0 0 11
inner:
    add     %tg3, %tl0, %tl2   ! %tl2 = k = ipnt + i
    sll     %tl2,   3, %tl2
    add     %tl2, %tg1, %tl1   ! %tl1 = &V[k]
    ldd     [%tl1], %tlf2     ! %tlf2,%tlf3 = V[k]
    add     %tl2, %tg0, %tl2   ! %tl2 = &X[k]
    ldd     [%tl2-8], %tlf8   ! %tlf8,%tlf9 = X[k-1]
    ldd     [%tl1+8], %tlf4   ! %tlf4,%tlf5 = V[k+1]
    ldd     [%tl2+8], %tlf6   ! %tlf6,%tlf7 = X[k+1]
    ldd     [%tl2+0], %tlf0   ! %tlf0,%tlf1 = X[k]
    srl     %tl0,   1, %tl0
    add     %tg2, %tl0, %tl0
    sll     %tl0,   3, %tl0
    add     %tl0, %tg0, %tl0   ! %tl0 = &X[ipntp + i / 2]
    fmuld   %tlf2, %tlf8, %tlf2; swch
    fmuld   %tlf4, %tlf6, %tlf4; swch
    faddd   %tlf2, %tlf4, %tlf2; swch
    fsubd   %tlf0, %tlf2, %tlf2; swch
    std     %tlf2, [%tl0]
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8    
    .align 64
V:  .skip 1001 * 8
