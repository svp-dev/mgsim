/*
 This test has not every core request a global from the pipeline.
 This is to test if a remote global request is properly forwarded.
*/
    .file "sparse_globals.s"
    .set noat
    .text
    
    .globl main
    .ent main
main:
    allocate $31, $2
    setlimit $2, 4
    cred $2, foo
    
    putg 42, $2, 0
    
    # Sync
    sync    $2, $0
    release $2
    mov     $0, $31
    end
    .end main

    .ent foo
    .registers 1 0 2 0 0 0
foo:
    subq $l0, 2, $l0
    bne $l0, 1f
    print $g0, 0
1:  nop
    end
    .end foo
    
