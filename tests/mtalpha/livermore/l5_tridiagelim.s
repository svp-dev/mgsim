/*
    Livermore kernel 5 -- Tri-diagonal elimination, below diagonal

    double x[1001], y[1001], z[1001];

    for(int i = 1; i < N; i++)
    {
        x[i] = z[i] * (y[i] - x[i - 1]);
    }
*/
    .file "l5_tridiagelim.s"
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1001\0"
	
    .text    
#
# Main Thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldpc     $27
    ldgp    $29, 0($27)
    
    allocate/s $31, 0, $4
    setstart $4, 1
    setlimit $4, $10
    cred    $4, loop
    
    ldah    $0, X($29)          !gprelhigh
    lda     $0, X($0)           !gprellow
    putg    $0, $4, 0           # $g0  = X
    
    ldah    $0, Y($29)          !gprelhigh
    lda     $0, Y($0)           !gprellow
    putg    $0, $4, 1           # $g1  = Y
    
    ldah    $0, Z($29)          !gprelhigh
    lda     $0, Z($0)           !gprellow
    putg    $0, $4, 2           # $g2  = Z

    ldt     $f0, 0($0)
    fputs   $f0, $4, 0; swch    # $df0 = X[0]
    
    sync    $4, $0
    release $4
    mov     $0, $31
    end
    .end main

#    
# Loop thread
#
# $g0  = X
# $g1  = Y
# $g2  = Z
# $df0 = X[i-1]
# $l0  = i
    .ent loop
    .registers 3 0 2 0 1 2
loop:
    s8addq  $l0, $g1, $l1
    ldt     $lf0, 0($l1)            # $lf0 = Y[i]
    s8addq  $l0, $g2, $l1
    ldt     $lf1, 0($l1)            # $lf1 = Z[i]
    s8addq  $l0, $g0, $l1           # $l0 = &X[i]
    subt    $lf0, $df0, $lf0; swch  # $lf0 = Y[i] - X[i-1]
    mult    $lf1, $lf0, $lf0; swch  # $lf0 = X[i]
    stt     $lf0, 0($l1); swch
    fmov    $lf0, $sf0              # $sf0 = X[i]
    end
    .end loop

    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
    .align 6
Z:  .skip 1001 * 8
    
