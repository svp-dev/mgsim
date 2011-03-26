/*
 This test does a delegation flood.
 It creates a family locally which all do a delegation to a specific core, the delegations
 that cannot immediately succeed should be queued.
 */
    .file "delegation_flood.s"
    .set noat
    .text

    .globl main
    .ent main
main:
    lda      $2, 1024($31)
    getcid   $1
    sll      $1, 1, $1
    or       $1, 1, $1  # Local
    allocate/s $1, 0, $1
    setblock $1, 128
    setlimit $1, $2
    cred     $1, foo
        
    sync     $1, $0
    release  $1
    mov      $0, $31
    end
    .end main
    
    .ent foo
    .registers 0 0 2 0 0 0
foo:
    mov (1 << 1) | 1, $l0      # PID:1, Size=1
    allocate/s $l0, 0, $l0
    cred     $l0, bar
    sync     $l0, $l1
    release  $l0
    mov      $l1, $31
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
    .ascii "PLACES: 2\0"
