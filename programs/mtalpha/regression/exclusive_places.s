/*
 This test test exclusive delegation. A shared, global variable is incremented
 on an exclusive place and summed in its parent. Without exclusivity, the
 result would not be correct due to overlapping increments.
 */
    .file "exclusive_places.s"
    .set noat
    
    .globl main
    .ent main
main:
    ldgp $29, 0($27)
    
    allocate 2, $2  # place = LOCAL
    setlimit $2, 64
    cred    $2, foo
    
    putg    $29, $2, 0      # {$g0} = GP
    puts    $31, $2, 0      # {$d0} = sum = 0
    
    # Sync
    sync    $2, $0
    mov     $0, $31; swch
    gets    $2, 0, $1       # $1 = {$s0}
    release $2

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
    .registers 1 1 3 0 0 0
foo:
    allocate (1 << 4) | (1 << 3) | (3 << 1) | 1, $l2     # PID:1, Delegated, Suspend, Exclusive
    cred     $l2, bar
    swch
    putg     $g0, $l2, 0        # {$g0} = GP
    swch
    sync     $l2, $l0
    mov      $l0, $31
    swch
    gets     $l2, 0, $l1        # $l1 = {$s0}
    release  $l2
    addq     $d0, $l1, $s0
    end
    .end foo
    
# $g0 = GP
# $s0 = return value
    .ent bar
    .registers 1 1 2 0 0 0
bar:
    lda  $l0, val($g0)   !gprellow
    ldah $l0, val($l0)   !gprelhigh
    ldl  $l1, 0($l0)
    addq $l1, 1, $l1
    stl  $l1, 0($l0)
    mov  $l1, $s0
    end
    .end bar

    .data
val:
    .int 0

    .ascii "PLACES: 1,{1,2,3,4}\0"
