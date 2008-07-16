# Livermore kernel 7 -- Equation of state fragment
#
#    int u[N + U], x[N], y[N], z[N];
#
#    for(int i = 0; i < N; i++) {
#        x[i] = u[i+0] + R * (z[i+0] + R * y[i+0]) +
#          T * (u[i+3] + R * (u[i+2] + R * u[i+1]) +
#          T * (u[i+6] + Q * (u[i+5] + Q * u[i+4])));
#    }
#
    .file "l7_eqofstatefrag.s"
    .text
    
    .equ MAX_N, 1048576
    .equ Q, 10
    .equ R, 20
    .equ T, 40

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
    ldah    $2, Z($29)      !gprelhigh
    lda     $2, Z($2)       !gprellow   # $2 = Z
    ldah    $3, U($29)      !gprelhigh
    lda     $3, U($3)       !gprellow   # $3 = U
    
    subq $10, 1, $10
    
    allocate $4
    setlimit $4, $10
    cred $4, loop
    mov $4, $31
    end
    .end main

#
# Loop thread
# $g0 = X
# $g1 = Y
# $g2 = Z
# $g3 = U
# $l0 = i
#
    .globl loop
    .ent loop
loop:
    .registers 4 0 10 0 0 0
    s4addq  $l0, $g3, $l1   # $l1 = &u[i]
    ldl     $l2, 16($l1)    # $l2 = u[i+4]
    ldl     $l3, 20($l1)    # $l3 = u[i+5]   
    ldl     $l4, 24($l1)    # $l4 = u[i+6]
    ldl     $l5,  4($l1)    # $l5 = u[i+1]
    ldl     $l6,  8($l1)    # $l6 = u[i+2]
    ldl     $l7, 12($l1)    # $l7 = u[i+3]
    s4addq  $l0, $g1, $l8
    ldl     $l8, 0($l8)     # $l8 = y[i]
    s4addq  $l0, $g2, $l9
    ldl     $l9, 0($l9)     # $l9 = z[i]
    s4addq  $l0, $g0, $l0   # $l0 = &x[i]
    ldl     $l1,  0($l1)    # $l1 = u[i+0]
    
    mull    $l2,   Q, $l2; swch
    addl    $l2, $l3, $l2; swch
    mull    $l2,   Q, $l2
    addl    $l2, $l4, $l2; swch
    mull    $l2,   T, $l2
    
    mull    $l5,   R, $l5; swch
    addl    $l5, $l6, $l5; swch
    mull    $l5,   R, $l5
    addl    $l5, $l2, $l2
    addl    $l2, $l7, $l2; swch
    mull    $l2,   T, $l2
    
    mull    $l8,   R, $l8; swch
    addl    $l8, $l9, $l8; swch
    mull    $l8,   R, $l8
    addl    $l8, $l2, $l2
    addl    $l2, $l1, $l1; swch
    
    stl     $l1, 0($l0)
    end
    .end loop


    .section .bss
    .align 6
X:  .skip MAX_N * 4
Y:  .skip MAX_N * 4
Z:  .skip MAX_N * 4
U:  .skip (MAX_N + 6) * 4
