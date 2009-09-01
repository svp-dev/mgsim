/*
 This test does a delegation flood.
 It creates a family locally which all do a delegation to a specific core, the delegations
 that cannot immediately succeed should run locally.
 */
    .file "delegation_flood.s"
    .text

    .globl main
    .ent main
main:
    mov      2, $1
    lda      $2, 1024($31)
    allocate $1, 0, 0, 0, 0
    setblock $1, 128
    setlimit $1, $2
    cred     $1, foo
    mov      $1, $31
    end
    .end main
    
    .ent foo
    .registers 0 0 1 0 0 0
foo:
    mov      (1 << 3) | (2 << 1), $l0
    allocate $l0, 0, 0, 0, 0
    cred     $l0, bar
    mov      $l0, $31
    end
    .end foo

    .ent bar
    .registers 0 0 1 0 0 0
bar:
    # Do some work to tie up the resource
    lda $l0, 16($31)
1:  subq $l0, 1, $l0
    bne $l0, 1b
    end
    .end bar

    .data
    .ascii "PLACES: 1,1\0"
