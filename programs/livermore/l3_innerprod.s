# Livermore kernel 3 -- Inner product
#
    .file "l3_innerprod.s"
    .text
    
    .equ MAX_N, 1048576

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
    clr     $2                          # $2 = sum = 0
    
    allocate $3
    subq    $10, 1, $10
    setlimit $3, $10
    cred    $3, loop

    mov     $3, $31
    end
    .end main

#
# Loop thread
#
# $g0 = X
# $g1 = Y
# $d0 = sum
# $l0 = i
    .globl loop
    .ent loop
loop:
    .registers 2 1 3 0 0 0
    s4addq $l0, $g1, $l1
    ldl $l1, 0($l1)
    s4addq $l0, $g0, $l0
    ldl $l0, 0($l0)
    mull $l0, $l1, $l0; swch
    addq $d0, $l0, $s0
    end
    .end loop

    .section .bss

    .align 6
X:  .skip MAX_N * 4

    .align 6
Y:  .skip MAX_N * 4
