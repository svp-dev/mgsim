/*
 This test test exclusive delegation. A shared, global variable is incremented
 on an exclusive place and summed in its parent. Without exclusivity, the
 result would not be correct due to overlapping increments.
 */
    .file "exclusive_places.s"
    
    .globl main
    .ent main
main:
    ldgp $0, 0($27)
    mov 0, $1
    
    allocate $2, 0, 0, 0, 0
    setlimit $2, 64
    setplace $2, 2  # Local
    cred $2, foo
    
    # Sync
    mov $2, $31; swch

    # Check if the result matches
    lda $2, 2080($31)
    subq $1, $2, $1
    beq $1, 1f
    halt        # Cause an invalid instruction
1:  nop
    end
    .end main

# $g0     = GP
# $s0/$d0 = accumulator
    .ent foo
foo:
    .registers 1 1 3 0 0 0
    mov      $g0, $l0
    allocate $l2, 0, 0, 0, 0
    setplace $l2, (1 << 3) | (2 << 1) | 1   # Delegated, exclusive
    swch
    cred     $l2, bar
    mov      $l2, $31; swch
    addq     $d0, $l1, $s0
    end
    .end foo
    
# $g0 = GP
# $s0 = return value
    .ent bar
bar:
    .registers 1 1 2 0 0 0
    lda  $l0, val($g0)   !gprellow
    ldah $l0, val($l0)   !gprelhigh
    ldl  $l1, 0($l0)
    addq $l1, 1, $s0
    stl  $s0, 0($l0)
    end
    .end bar

    .data
val:
    .int 0

    .ascii "PLACES: 1,{1,2,3,4}\0"
