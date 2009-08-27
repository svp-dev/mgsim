/*
 This test has not every core request a global from the pipeline.
 This is to test if a remote global request is properly forwarded.
*/
    .file "sparse_globals.s"
    .text
    
    .globl main
    .ent main
main:
    mov 42, $0
    
    clr      $2
    allocate $2, 0, 0, 0, 0
    setlimit $2, 4
    cred $2, foo
    
    # Sync
    mov $2, $31
    end
    .end main

    .ent foo
foo:
    .registers 1 0 2 0 0 0
    subq $l0, 2, $l0
    bne $l0, 1f
    print $g0, 0
1:  nop
    end
    .end foo
    
