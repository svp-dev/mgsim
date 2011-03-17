/*
    Livermore kernel 3 -- Inner product

    double z[1001], x[1001];
    double q = 0.0;
    for (int k = 0; k < n; k++)
    {
        q += z[k] * x[k];
    }
*/
    .file "l3_innerprod.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:512\0"

    .text
    
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
    
    allocateng %0, %4
    setlimitng %4, %11
    cred    loop, %4
    
    putg    %1,  %4, 0
    putg    %2,  %4, 1
    fputs   %f0, %4, 0
    fputs   %f0, %4, 1      ! %tdf0,%tdf1 = sum = 0

    sync    %4, %1
    release %4
    mov     %1, %0
    end

!
! Loop thread
!
! %tg0 = X
! %tg1 = Y
! %td0 = sum
! %tl0 = i
    .globl loop
    .align 64
    .registers 2 0 2 0 2 4
loop:
    sll     %tl0, 3, %tl0
    add     %tl0, %tg1, %tl1
    ldd     [%tl1], %tlf2
    add     %tl0, %tg0, %tl0
    ldd     [%tl0], %tlf0
    fmuld   %tlf0, %tlf2, %tlf0; swch
    faddd   %tdf0, %tlf0, %tlf0; swch
    fmovs   %tlf0, %tsf0; swch
    fmovs   %tlf1, %tsf1
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
