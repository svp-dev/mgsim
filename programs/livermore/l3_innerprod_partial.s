# Livermore kernel 3 -- Inner product, with partial reduction
#
    .file "l3_innerprod_partial.s"
    .arch ev6
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
    
    # Calculate N / #procs
    itoft   $10, $f0
    cvtqt   $f0, $f0
    getinvprocs $f1
    mult    $f0, $f1, $f0
    cvttq   $f0, $f0; swch
    ftoit   $f0, $2                     # $2 = normal_size = floor(N / #procs)
    
    # Family runs from [0 ... #procs - 1]
    allocate $5, 0, 0, 0, 0
    getprocs $3
    subq     $3, 1, $4
    setlimit $5, $4
    
    # Calculate #procs that does one more
    mull    $2,  $3, $3
    subl    $10, $3, $3                 # $3 = num_more = N % #procs = N - normal_size * #procs
    
    clr     $4                          # $4 = sum = 0
    cred    $5, outer
    mov     $5, $31
    end
    .end main

#
# Outer loop thread
#
# $g0 = X
# $g1 = Y
# $g2 = normal_size
# $g3 = num_more
# $d0 = sum
# $l0 = i (0.. #procs - 1)
    .globl outer
    .ent outer
outer:
    .registers 4 1 4 0 0 0
    mulq    $l0, $g2, $l1       # $l1 = start
    addq    $g2,   1, $l2       # $l2 = more_size = normal_size + 1
    
    subq    $l0, $g3, $l3
    cmovgt  $l3, $g3, $l0       # $l0 = min(i, num_more)
    addq    $l1, $l0, $l1       # $l1 = actual start (accounting for +1)
    cmovge  $l3, $g2, $l2       # $l2 = actual size  (accounting for +1)
    addq    $l1, $l2, $l2
    subq    $l2,   1, $l2       # $l2 = limit
    
    allocate $l3, 0, 0, 0, 0
    setstart $l3, $l1
    setlimit $l3, $l2
    setplace $l3, 0     # Local family
    
    mov     $g0, $l0
    mov     $g1, $l1
    clr     $l2
    cred    $l3, loop
    mov     $l3, $31; swch
    addl    $d0, $l2, $s0
    end
    .end outer

#
# Local loop thread
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
