# Livermore kernel 5 -- Tri-diagonal elimination, below diagonal
#
#    int x[N], y[N], z[N];
#
#    for(int i = 1; i < N; i++) {
#        x[i] = z[i] * (y[i] - x[i - 1]);
#    }
#
    .file "l5_tridiagelim.s"
    .text
    
    .equ MAX_N, 1048576
    
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
    
    ldl     $3, 0($0)                   # $3 = X[0]
    
    allocate $4
    setlimit $4, $10
    mov     $3, $31                     # Wait for memory
    cred    $4, loop
    mov     $4, $31
    end
    .end main

#    
# Loop thread
#
# $g0 = X
# $g1 = Y
# $g2 = Z
# $d0 = X[i-1]
# $l0 = i
    .globl loop
    .ent loop
loop:
    .registers 3 1 3 0 0 0
    s4addq  $l0, $g1, $l1
    ldl     $l1, 0($l1)         # $l1 = Y[i]
    s4addq  $l0, $g2, $l2
    ldl     $l2, 0($l2)         # $l2 = Z[i]
    s4addq  $l0, $g0, $l0       # $l0 = &X[i]
    subl    $l1, $d0, $l1; swch # $l1 = Y[i] - X[i-1]
    mull    $l2, $l1, $s0; swch # $s0 = X[i]
    stl     $s0, 0($l0)
    end
    .end loop

    .section .bss
    .align 6
X:  .skip MAX_N * 4
    .align 6
Y:  .skip MAX_N * 4
    .align 6
Z:  .skip MAX_N * 4
    
