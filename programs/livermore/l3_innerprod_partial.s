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
    clr     $5                          # $5 = sum = 0
    
    # Calculate N / #procs
    getprocs $3
    addl    $10, $3, $2
    subl    $2, 1, $2
    itoft   $2, $f0
    cvtqt   $f0, $f0
    getinvprocs $f1
    mult    $f0, $f1, $f0
    cvttq   $f0, $f0; swch
    ftoit   $f0, $2                     # $2 = normal_size = ceil(N / #procs)
    
    # Calculate last index
    subl    $3,  1, $3
    mull    $2, $3, $3                  # $3 = last_index = normal_size * (#procs - 1)
    
    # Calculate last size
    subl    $10, $3, $4                 # $4 = last_size = N - last_index
    
    allocate $6
    setlimit $6, $3
    setstep  $6, $2
    cred    $6, outer

    mov     $6, $31
    end
    .end main

#
# Outer loop thread
#
# $g0 = X
# $g1 = Y
# $g2 = normal_size
# $g3 = last_index
# $g4 = last_size
# $d0 = sum
# $l0 = i
    .globl outer
    .ent outer
outer:
    .registers 5 1 4 0 0 0
    allocate $l3
    setstart $l3, $l0

    mov     $g2, $l1
    subq    $g3, $l0, $l2
    cmoveq  $l2, $g4, $l1   # $l1 = size = (i == last_index) ? last_size : normal_size    
    addq    $l0, $l1, $l0
    subq    $l0,   1, $l0   # $l0 = i + size - 1
    
    setlimit $l3, $l0
    setplace $l3, 0     # Local family
    
    mov     $g0, $l0
    mov     $g1, $l1
    clr     $l2
    cred    $l3, loop
    mov     $l3, $31; swch
    debug   $l2
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
    debug $l0
    addq $d0, $l0, $s0
    end
    .end loop

    .section .bss

    .align 6
X:  .skip MAX_N * 4

    .align 6
Y:  .skip MAX_N * 4
