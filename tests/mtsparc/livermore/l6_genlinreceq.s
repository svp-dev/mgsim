/*
    Livermore kernel 6 -- General linear recurrence equations

    double x[1001], y[64][64];

    for(int i = 1; i < N; i++) {
        double sum = 0;
        for(int j = 0; j < i; j++) {
            sum += y[j][i] * x[i - j - 1];
        }
        x[i] = x[i] + sum;
    }
*/
    .file "l6_genlinreceq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:16\0"

    .align 4
zero:
    .long 0
        
    .text
    
!
! Main thread
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
    setblock %5, 2
    set     outer, %1
    crei    %1, %5

    set     X, %1           ! %1 = X
    set     Y, %2           ! %2 = Y
    putg    %1, %5, 0
    putg    %2, %5, 1
    puts    %0, %5, 0       ! %3 = token
    set     zero, %1
    ld      [%1], %f0
    fputg   %f0, %5, 0
        
    sync    %5, %1
    release %5
    mov     %1, %0
    end
    
!
! Outer loop
!
! %tg0  = X
! %tg1  = Y
! %td0  = token
! %tl0  = i
!
    .globl outer
    .align 64
    .registers 2 1 5 1 0 4
outer:
    clr %tl3
    allocates %tl3

    sll     %tl0,    3, %tl1
    add     %tl1,  %tg1, %tl1  ! %tl1 = &Y[0][i]
    
    setlimit %tl3, %tl0; swch
    mov     %td0, %0
    set     inner, %tl4
    crei    %tl4, %tl3

    putg    %tl0, %tl3, 0
    putg    %tl1, %tl3, 1
    putg    %tg0, %tl3, 2     ! %tg2 = X
    fputs   %tgf0, %tl3, 0
    fputs   %tgf0, %tl3, 1     ! %tlf0,%tlf1 = sum = 0

    sll     %tl0,   3, %tl4
    add     %tl4, %tg0, %tl4   ! %tl4 = &X[i]
    ldd     [%tl4], %tlf2
    
    sync    %tl3, %tl0
    mov     %tl0, %0
    fgets   %tl3, 0, %tlf0
    fgets   %tl3, 1, %tlf1
    release %tl3
    
    faddd   %tlf2, %tlf0, %tlf0; swch  ! %tlf0,%tlf1 = X[i] + sum
    std     %tlf0, [%tl4]
    stbar
    clr     %ts0
    end
    
!
! Inner loop
!
! %tg0  = i
! %tg1  = &Y[i]
! %tg2  = X
! %tdf0,%tdf1 = sum
! %tl0  = j
!
    .globl inner
    .align 64
    .registers 3 0 2 0 2 4
inner:
    sub     %tg0,  %tl0, %tl1
    sll     %tl1,    3, %tl1
    add     %tl1,  %tg2, %tl1
    ldd     [%tl1-8], %tlf0       ! %tlf0,%tlf1 = X[i - j - 1]
    
    sll     %tl0,   9, %tl0
    add     %tl0, %tg1, %tl0
    ldd     [%tl0], %tlf2         ! %tlf2,%tlf3 = Y[i][j]
    
    fmuld   %tlf0, %tlf2, %tlf0; swch
    faddd   %tlf0, %tdf0, %tlf0; swch
    fmovs   %tlf0, %tsf0; swch
    fmovs   %tlf1, %tsf1
    end
    
    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 64 * 64 * 8
