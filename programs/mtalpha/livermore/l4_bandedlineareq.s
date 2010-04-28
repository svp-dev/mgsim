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
    
#
# Main thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldgp    $29, 0($27)
    
    lda     $4,    M
    lda     $5, 1001
    
    allocate $31, $3
    setstart $3, 6
    setlimit $3, $5
    setstep  $3, $4
    cred    $3, outer
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $3, 0       # $g0 = X

    ldah    $0, Y($29)      !gprelhigh
    lda     $0, Y($0)       !gprellow
    putg    $0, $3, 1       # $g1 = Y
    
    #
    # Calculate N / 5
    #
    # http://blogs.msdn.com/devdev/archive/2005/12/12/502980.aspx
    #
    # N / 5
    # = (2**32 / 5)  * N / 2**32
    # = 858993459.25 * N / 2**32 (approximated, but correct)
    # = (N * 0x33333333 + (N >> 2)) >> 32
    #
    ldah    $2, 0x3333($31)
    lda     $2, 0x3333($2)
    mulq    $10, $2,  $2
    srl     $10,  2, $10
    addq    $10, $2,  $2
    srl     $2,  32,  $2
    putg    $2,  $3,   2     # $g2 = N/5
    
    sync    $3, $0
    release $3
    mov     $0, $31
    end
    
    .end main
    
#
# Outer loop thread
#
# $g0 = X
# $g1 = Y
# $g2 = N/5
# $l0 = i
#
    .ent outer
    .registers 3 0 5 0 0 3
outer:
    allocate $31, $l3
    setlimit $l3, $g2; swch
    cred     $l3, inner
    
    putg    $l0, $l3, 0; swch   # $g0  = i
    putg    $g0, $l3, 1         # $g1  = X
    putg    $g1, $l3, 2         # $g2  = Y
    fputs   $f31, $l3, 0        # $df0 = temp = 0.0
    
    s8addq  $l0, $g0, $l4       # $l4 = &X[i]
    ldt     $lf1, -8($l4)       # $lf1 = X[i-1]
    ldt     $lf2, 32($g1)       # $lf2 = Y[4]
    
    sync    $l3, $l0
    mov     $l0, $31; swch
    fgets   $l3, 0, $lf0        # $sf0
    release $l3
    
    subt    $lf1, $lf0, $lf1; swch   # $lf1 = X[i-1] - temp
    mult    $lf2, $lf1, $lf2; swch
    stt     $lf2, -8($l4)
    end
    .end outer

#
# Inner loop thread
#
# $g0  = i
# $g1  = X
# $g2  = Y
# $df0 = temp
# $l0  = j
#
    .ent inner
    .registers 3 0 2 0 1 2
inner:
    addq    $g0, $l0, $l1   # $l1 = i + j
    s8addq  $l1, $g1, $l1   # $l1 = &X[i + j];
    ldt     $lf1, -48($l1)  # $lf1 = X[i + j - 6];
    
    sll     $l0,   2, $l1
    addq    $l1, $l0, $l0   # $l0 = j * 5;
    s8addq  $l0, $g2, $l0   # $l0 = &Y[j * 5];
    ldt     $lf0, 32($l0)   # $lf0 = Y[j * 5 + 4];
    
    mult    $lf0, $lf1, $lf0; swch
    addt    $df0, $lf0, $lf0; swch
    fmov    $lf0, $sf0
    end
    .end inner

    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
