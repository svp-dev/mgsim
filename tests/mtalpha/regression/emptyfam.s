/*
 This test checks that resources are properly released when creating
 families with 0 threads.
 */
    .file "emptyfam.s"
    .set noat
    
    .globl main
    .ent main
main:
    lda $0, 500($31)
    mov 3, $3

loop:
    allocate/s $3, 0, $1
    setlimit $1, 0
    cred     $1, foo
    swch
    sync    $1, $2
    swch
    release $1
    swch
    mov     $2, $31

    subq    $0, 1, $0 
    bne     $0, loop
    nop
    end
    .end main

    .ent foo
    .registers 0 0 1 0 0 0
foo:
    nop
    end
    .end foo

    .ascii "PLACES: 1\0"
