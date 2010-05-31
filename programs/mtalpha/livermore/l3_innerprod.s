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
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:512\0"
	
    .text
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
    
    allocate $31, $3
    setlimit $3, $10
    cred     $3, loop

    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow
    putg    $0, $3, 0       # $g0 = X

    ldah    $0, Y($29)      !gprelhigh
    lda     $0, Y($0)       !gprellow
    putg    $0, $3, 1       # $g1 = Y
    
    fputs   $f31, $3, 0     # $df0 = sum = 0

    sync     $3, $0
    release  $3
    mov      $0, $31
    end
    .end main

#
# Loop thread
#
# $g0  = X
# $g1  = Y
# $df0 = sum
# $l0  = i
    .ent loop
    .registers 2 0 2 0 1 2
loop:
    s8addq  $l0, $g1, $l1
    ldt     $lf1, 0($l1)
    s8addq  $l0, $g0, $l0
    ldt     $lf0, 0($l0)
    mult    $lf0, $lf1, $lf0; swch
    addt    $df0, $lf0, $lf0; swch
    fmov    $lf0, $sf0
    end
    .end loop

    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
