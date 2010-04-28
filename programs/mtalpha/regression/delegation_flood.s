/*
 This test does a delegation flood.
 It creates a family locally which all do a delegation to a specific core, the delegations
 that cannot immediately succeed should be queued.
 */
    .file "delegation_flood.s"
    .text

    .globl main
    .ent main
main:
    lda      $2, 1024($31)
    allocate (2 << 1), $1      # Local
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
    allocate (1 << 4) | (1 << 3) | (3 << 1), $l0     # PID:1, Suspend:1, Type:Delegate, Exclusive:0
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
    .ascii "PLACES: 1,1\0"
