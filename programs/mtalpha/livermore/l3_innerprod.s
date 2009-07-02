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
#
# Main thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldah    $29, 0($27)     !gpdisp!1
    lda     $29, 0($29)     !gpdisp!1

    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow   # $0 = X
    ldah    $1, Y($29)      !gprelhigh
    lda     $1, Y($1)       !gprellow   # $1 = Y
    fclr    $f0                         # $f0 = sum = 0
    
    allocate $3, 0, 0, 0, 0
    setlimit $3, $10
    cred    $3, loop

    mov     $3, $31
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
loop:
    .registers 2 0 2 0 1 2
    s8addq  $l0, $g1, $l1
    ldt     $lf1, 0($l1)
    s8addq  $l0, $g0, $l0
    ldt     $lf0, 0($l0)
    mult    $lf0, $lf1, $lf0; swch
    addt    $df0, $lf0, $sf0
    end
    .end loop

    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
