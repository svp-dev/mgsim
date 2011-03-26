/*
    Livermore kernel 5 -- Tri-diagonal elimination, below diagonal

    double x[1001], y[1001], z[1001];

    for(int i = 1; i < N; i++)
    {
        x[i] = z[i] * (y[i] - x[i - 1]);
    }
*/
    .file "l5_tridiagelim.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1001\0"

    .text
    
!
! Main Thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    clr %5
    allocates %5
    setstart %5, 1
    setlimit %5, %11
    set     loop, %1
    crei    %1, %5

    set     X, %1       ! %1 = X
    ldd     [%1], %f2   ! %f2,%f3 = X[0]
    set     Y, %2       ! %2 = Y
    set     Z, %3       ! %3 = Z
    
    putg    %1, %5, 0
    putg    %2, %5, 1
    putg    %3, %5, 2
    fputs   %f2, %5, 0
    fputs   %f3, %5, 1

    sync    %5, %1
    release %5
    mov     %1, %0
    end

!    
! Loop thread
!
! %tg0 = X
! %tg1 = Y
! %tg2 = Z
! %tdf0,df1 = X[i-1]
! %tl0 = i
    .align 64
    .registers 3 0 2 0 2 5
loop:
    sll     %tl0,   3, %tl0
    add     %tl0, %tg1, %tl1
    ldd     [%tl1], %tlf1             ! %tlf1,%tlf2 = Y[i]
    add     %tl0, %tg2, %tl1
    ldd     [%tl1], %tlf3             ! %tlf3,%tlf4 = Z[i]
    add     %tl0, %tg0, %tl1           ! %tl1 = &X[i]
    fsubd   %tlf1, %tdf0, %tlf1; swch  ! %tlf1,%tlf2 = Y[i] - X[i-1]
    fmuld   %tlf3, %tlf1, %tlf1; swch  ! %tlf1,%tlf2 = X[i]
    std     %tlf1, [%tl1]; swch
    fmovs   %tlf1, %tsf0
    fmovs   %tlf2, %tsf1
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
    .align 64
Z:  .skip 1001 * 8
    
