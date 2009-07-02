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
    .arch ev6

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:16\0"
	
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
    clr     $2                          # $2 = token
    
    allocate $4, 0, 0, 0, 0
    setstart $4, 1
    setlimit $4, $10
    setblock $4, 2
    setplace $4, 2      # Local
    cred    $4, outer
    mov     $4, $31
    end
    .end main
    
#
# Outer loop
#
# $g0  = X
# $g1  = Y
# $d0  = token
# $l0  = i
#
    .ent outer
outer:
    .registers 2 1 6 0 0 2
    
    allocate $l3, 0, 0, 0, 0

    s8addq  $l0,  $g1, $l1  # $l1 = &Y[0][i]
    mov     $g0,  $l2       # $l2 = X
    fclr    $lf0            # $lf0 = sum = 0
    
    setlimit $l3, $l0; swch
    mov     $d0, $31; swch
    cred    $l3, inner
    s8addq  $l0, $g0, $l4   # $l4 = &X[i]
    ldt     $lf1, 0($l4)
    mov     $l3, $31
    addt    $lf1, $lf0, $lf0; swch # $lf0 = X[i] + sum
    stt     $lf0, 0($l4); swch
    wmb
    clr     $s0
    end
    .end outer
    
#
# Inner loop
#
# $g0  = i
# $g1  = &Y[0][i]
# $g2  = X
# $df0 = sum
# $l0  = j
#
    .ent inner
inner:
    .registers 3 0 2 0 1 2
    subq    $g0,  $l0, $l1
    s8addq  $l1,  $g2, $l1
    ldt     $lf0, -8($l1)       # $lf0 = X[i - j - 1]
    
    sll     $l0,   6, $l0
    s8addq  $l0, $g1, $l0
    ldt     $lf1, 0($l0)        # $lf1 = Y[j][i]
    
    mult    $lf0, $lf1, $lf0; swch
    addt    $lf0, $df0, $sf0
    end
    .end inner
    
    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 64 * 64 * 8
