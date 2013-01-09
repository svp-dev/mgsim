/*
    Livermore kernel 4 -- Banded linear equations

    int m = (1001 - 7) / 2;
    double x[1001], y[1001];

    for (int i = 6; i < 1001; i += m)
    {
        double temp = 0;
        for(int j = 0; j < N/5; j++) {
            temp += x[i + j - 6] * y[j * 5 + 4];
        }

        x[i - 1] = y[4] * (x[i - 1] - temp);
    }
*/
    .file "l4_bandedlineareq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:256\0"

    .align 4
zero:
    .long 0
        
    .text
    
    .equ M, (1001 - 7) / 2
    
!
! Main thread
!
! %11 = N
!
    .globl main
    .align 64
main:
    set     X, %1       ! %1 = X
    set     Y, %2       ! %2 = Y   
    udiv    %11, 5, %3  ! %3 = N / 5
    
    set     M, %5       ! %5 = M
    set     1001, %6    ! %6 = 1001

    clr %4
    allocates %4
    setstart %4, 6
    setlimit %4, %6
    setstep  %4, %5
    set     outer, %5
    crei    %5, %4

    set     zero, %5
    ld      [%5], %f0
        

    putg    %1, %4, 0
    putg    %2, %4, 1
    putg    %3, %4, 2
    fputg   %f0, %4, 0
    
    sync    %4, %1
    release %4
    mov     %1, %0
    end
    
!
! Outer loop thread
!
! %tg0 = X
! %tg1 = Y
! %tg2 = N/5
! %tfg0 = 0        
! %tl0 = i
!
    .align 64
    .registers 3 0 3 1 0 7
outer:
    clr %tl1
    allocates %tl1
    setlimit %tl1, %tg2; swch
    set      inner, %tl2
    crei     %tl2, %tl1
    
    putg     %tl0, %tl1, 0     ! %tg0 = i
    putg     %tg0, %tl1, 1     ! %tg1 = X
    putg     %tg1, %tl1, 2     ! %tg2 = Y

    fputs    %tgf0, %tl1, 0
    fputs    %tgf0, %tl1, 1     ! %tdf0,%tdf1 = temp = 0
    
    sll     %tl0, 3, %tl2
    add     %tl2, %tg0, %tl2    ! %tl5 = &X[i]
    ldd     [%tl2-8],  %tlf2   ! %tlf2,%tlf3 = X[i-1]
    ldd     [%tg1+32], %tlf4   ! %tlf4,%tlf5 = Y[4]
    
    sync    %tl1, %tl0
    mov     %tl0, %0; swch
    fgets   %tl1, 0, %tlf0
    fgets   %tl1, 1, %tlf1
    release %tl1
    fsubd   %tlf2, %tlf0, %tlf2; swch   ! %tlf2,%tlf3 = X[i-1] - temp
    fmuld   %tlf4, %tlf2, %tlf4; swch
    std     %tlf4, [%tl2-8]
    end

!
! Inner loop thread
!
! %tg0 = i
! %tg1 = X
! %tg2 = Y
! %tdf0,df1 = temp
! %tl0 = j
!
    .align 64
    .registers 3 0 2 0 2 4
inner:
    add     %tg0, %tl0, %tl1   ! %tl1 = i + j
    sll     %tl1,   3, %tl1
    add     %tl1, %tg1, %tl1   ! %tl1 = &X[i + j];
    ldd     [%tl1-48], %tlf2  ! %tlf2,%tlf3 = X[i + j - 6];
    
    sll     %tl0,   2, %tl1
    add     %tl1, %tl0, %tl0   ! %tl0 = j * 5;
    sll     %tl0,   3, %tl0
    add     %tl0, %tg2, %tl0   ! %tl0 = &Y[j * 5];
    ldd     [%tl0+32], %tlf0  ! %tlf0,%tlf1 = Y[j * 5 + 4];
    
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
