/*
    Livermore kernel 1 -- Hydro fragment

    double X[1001], Y[1001], Z[1001];
        
    for (k=0 ; k < N; k++)
    {
        x[k] = Q + Y[k] * (R * Z[k+10] + T * Z[k+11]);
    }
*/

    .file "l1_hydro.s"
    .arch ev6
    .text

#
# Constants
#
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
    ldgp     $29, 0($27)    # $29 = GP
    
    mov      Q, $0; itoft $0, $f0; cvtqt $f0, $f0   # $f0 = Q
    mov      R, $0; itoft $0, $f1; cvtqt $f1, $f1   # $f1 = R
    mov      T, $0; itoft $0, $f2; cvtqt $f2, $f2   # $f2 = T
    
    ldah     $0, X($29)     !gprelhigh
    lda      $0, X($0)      !gprellow   # $0  = X
    ldah     $1, Y($29)     !gprelhigh
    lda      $1, Y($1)      !gprellow   # $1  = Y
    ldah     $2, Z($29)     !gprelhigh
    lda      $2, Z($2)      !gprellow   # $2  = Z
    
    clr      $3
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
# $g0  = X
# $g1  = Y
# $g2  = Z
# $gf0 = Q
# $gf1 = R
# $gf2 = T
# $l0  = i
#
    .globl loop
    .ent loop
    .registers 3 0 2  3 0 3     # GR,SR,LR, GF,SF,LF
loop:
    s8addq  $l0, $g2, $l1   # $l1  = &Z[i]
    ldt     $lf1, 80($l1)   # $lf1 = Z[i + 10]
    ldt     $lf2, 88($l1)   # $lf2 = Z[i + 11]
    s8addq  $l0, $g1, $l1   # $l1  = &Y[i]
    ldt     $lf0,  0($l1)   # $lf0 = Y[i]
    s8addq  $l0, $g0, $l0   # $l0  = &X[i]

    mult    $gf1, $lf1, $lf1; swch
    mult    $gf2, $lf2, $lf2; swch
    addt    $lf1, $lf2, $lf1; swch
    mult    $lf0, $lf1, $lf0; swch
    addt    $lf0, $gf0, $lf0; swch
    stt     $lf0, 0($l0)
    end   
    .end loop

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:128\0"

#
# Data
#
    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
    .align 6
Z:  .skip 1001 * 8
