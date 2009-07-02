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
#
# Main Thread
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
    ldah    $2, Z($29)      !gprelhigh
    lda     $2, Z($2)       !gprellow   # $2 = Z
    
    ldt     $f0, 0($0)                  # $f0 = X[0]
    
    allocate $4, 0, 0, 0, 0
    setstart $4, 1
    setlimit $4, $10
    cred    $4, loop
    mov     $4, $31
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
    mult    $lf1, $lf0, $sf0; swch  # $sf0 = X[i]
    stt     $sf0, 0($l1)
    end
    .end loop

    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
    .align 6
Z:  .skip 1001 * 8
    
