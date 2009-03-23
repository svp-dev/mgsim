#
# Livermore kernel 1 -- Hydro fragment
#
    .file "l1_hydro.s"
    .text

#
# Constants
#
    .equ MAX_N, 1048576
    
    .equ Q, 100
    .equ R, 5
    .equ T, 2

#
# Main thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldah    $29, 0($27)    !gpdisp!1
    lda     $29, 0($29)    !gpdisp!1   # $29 = GP

    ldah    $0, X($29)     !gprelhigh
    lda     $0, X($0)      !gprellow   # $0 = X
    ldah    $1, Y($29)     !gprelhigh
    lda     $1, Y($1)      !gprellow   # $1 = Y
    ldah    $2, Z($29)     !gprelhigh
    lda     $2, Z($2)      !gprellow   # $2 = Z

    allocate $3, 0, 0, 0, 0 # Start = 0, Step = 1
    setlimit $3, $10        # Limit = N
    cred     $3, loop
    mov      $3, $31        # Sync
    end
    .end main

#
# Loop thread:
# x[i] = Q + Y[i] * (R * Z[i + 10] + T * Z[i + 11]);
#
# $g0 = X
# $g1 = Y
# $g2 = Z
# $l0 = i
#
    .globl loop
    .ent loop
loop:
    .registers 3 0 4  0 0 0     # GR,SR,LR, GF,SF,LF

    s4addq  $l0, $g2, $l3   # $l3 = &Z[i]
    ldl     $l2, 40($l3)    # $l2 = Z[i + 10]
    ldl     $l3, 44($l3)    # $l3 = Z[i + 11]
    s4addq  $l0, $g1, $l1   # $l1 = &Y[i]
    ldl     $l1,  0($l1)    # $l1 = Y[i]
    s4addq  $l0, $g0, $l0   # $l0 = &X[i]

    mull    $l2, R, $l2; swch
    mull    $l3, T, $l3; swch
    addl    $l2, $l3, $l2
    mull    $l1, $l2, $l1; swch
    addl    $l1, Q, $l1
    stl     $l1, 0($l0)

    end   
    .end loop

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"

#
# Data
#
    .section .bss
    .align 6
X:  .skip MAX_N*4

    .align 6
Y:  .skip MAX_N*4

    .align 6
Z:  .skip (MAX_N + 11)*4

