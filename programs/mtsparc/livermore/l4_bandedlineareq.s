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
    
    allocates %0, %4
    setstart %4, 6
    setlimit %4, %6
    setstep  %4, %5
    cred    outer, %4
    
    putg    %1, %4, 0
    putg    %2, %4, 1
    putg    %3, %4, 2
    
    sync    %4, %1
    release %4
    mov     %1, %0
    end
    
!
! Outer loop thread
!
! %g0 = X
! %g1 = Y
! %g2 = N/5
! %l0 = i
!
    .align 64
    .registers 3 0 3 0 0 6
outer:
    allocates %0, %l1
    setlimit %l1, %g2; swch
    cred     inner, %l1
    
    putg     %l0, %l1, 0     ! %g0 = i
    putg     %g0, %l1, 1     ! %g1 = X
    putg     %g1, %l1, 2     ! %g2 = Y
    fputs    %f0, %l1, 0
    fputs    %f0, %l1, 1     ! %df0,%df1 = temp = 0
    
    sll     %l0, 3, %l2
    add     %l2, %g0, %l2    ! %l5 = &X[i]
    ldd     [%l2-8],  %lf2   ! %lf2,%lf3 = X[i-1]
    ldd     [%g1+32], %lf4   ! %lf4,%lf6 = Y[4]
    
    sync    %l1, %l0
    mov     %l0, %0; swch
    fgets   %l1, 0, %lf0
    fgets   %l1, 1, %lf1
    release %l1
    fsubd   %lf2, %lf0, %lf2; swch   ! %lf2,%lf3 = X[i-1] - temp
    fmuld   %lf4, %lf2, %lf4; swch
    std     %lf4, [%l2-8]
    end

!
! Inner loop thread
!
! %g0 = i
! %g1 = X
! %g2 = Y
! %df0,df1 = temp
! %l0 = j
!
    .align 64
    .registers 3 0 2 0 2 4
inner:
    add     %g0, %l0, %l1   ! %l1 = i + j
    sll     %l1,   3, %l1
    add     %l1, %g1, %l1   ! %l1 = &X[i + j];
    ldd     [%l1-48], %lf2  ! %lf2,%lf3 = X[i + j - 6];
    
    sll     %l0,   2, %l1
    add     %l1, %l0, %l0   ! %l0 = j * 5;
    sll     %l0,   3, %l0
    add     %l0, %g2, %l0   ! %l0 = &Y[j * 5];
    ldd     [%l0+32], %lf0  ! %lf0,%lf1 = Y[j * 5 + 4];
    
    fmuld   %lf0, %lf2, %lf0; swch
    faddd   %df0, %lf0, %lf0; swch
    fmovs   %lf0, %sf0; swch
    fmovs   %lf1, %sf1
    end

    .section .bss
    .align 64
X:  .skip 1001 * 8
    .align 64
Y:  .skip 1001 * 8
