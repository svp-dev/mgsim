# Livermore kernel 6 -- General linear recurrence equations
#
#    double x[N], y[N][N];
#
#    for(int i = 1; i < N; i++) {
#        double sum = 0.01;
#        for(int j = 1; j <= i; j++) {
#            sum += y[i][j - 1] * x[i - j];
#        }
#        x[i] = sum;
#    }
#
    .file "l6_genlinreceq.s"
    .arch ev6
    .text
    
    .equ MAX_N, 1024
    
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
    lda     $2, MAX_N($31)              # $2 = MAX_N
    mov     $31, $3                     # $3 = token
    subq    $10, 1, $10
    
    # Load 0.01 into $f0
    # It's 0x3F847AE147AE147B in IEEE 754
    ldah    $4, 0x3F84($31)
    lda     $4, 0x7AE1($4)
    sll     $4, 32
    ldah    $4, 0x47AE($4)
    lda     $4, 0x147B($4)
    itoft   $4, $f0
    
    allocate $4
    setstart $4, 1
    setlimit $4, $10
    cred    $4, outer
    mov     $4, $31
    end
    .end main
    
#
# Outer loop
#
# $g0  = X
# $g1  = Y
# $g2  = MAX_N
# $d0  = token
# $l0  = i
# $gf0 = 0.01
#
    .globl outer
    .ent outer
outer:
    .registers 3 1 5 1 0 1
    
    allocate $l3

    mull    $l0,  $g2, $l1
    s8addq  $l1,  $g1, $l1  # $l1 = &Y[i]
    mov     $g0,  $l2       # $l2 = X
    fmov    $gf0, $lf0      # $lf0 = 0.01
    
    setstart $l3,   1; swch
    setlimit $l3, $l0
    mov     $d0, $31
    cred    $l3, inner
    s8addq  $l0, $g0, $l4   # $l4 = &X[i]
    mov     $l3, $31
    stt     $lf0, 0($l4)
    wmb
    mov     $31, $s0
    end
    .end outer
    
#
# Inner loop
#
# $g0  = i
# $g1  = &Y[i]
# $g2  = X
# $df0 = sum
# $l0  = j
#
    .globl inner
    .ent inner
inner:
    .registers 3 0 2 0 1 2
    subq    $g0,  $l0, $l1
    s8addq  $l1,  $g2, $l1
    ldt     $lf0, 0($l1)        # $lf0 = X[i - j - 1]
    
    s8addq  $l0, $g1, $l0
    ldt     $lf1, -8($l0)       # $lf1 = Y[i][j]
    
    mult    $lf0, $lf1, $lf0; swch
    addt    $lf0, $df0, $lf0; swch
    fmov    $lf0, $sf0
    end
    .end inner
    
    
    .section .bbs
    .align 6
X:  .skip MAX_N * 8
    .align 6
Y:  .skip MAX_N * MAX_N * 8
