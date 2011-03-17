/*
 This test has not every core request a global from the pipeline.
 This is to test if a remote global request is properly forwarded.
*/
    .file "sparse_globals.s"
    .text
    
    .globl main
    .align 64
main:
    mov 42, %1
    
    allocateng %0, %2
    setlimitng %2, 4
    cred foo, %2
    
    putg %1, %2, 0
    
    ! Sync
    sync %2, %1
    release %2
    mov %1, %0
    end

    .align 64
    .registers 1 0 2 0 0 0
foo:
    cmp %tl0, 2
    bne 1f
    print %tg0, 0
1:  nop
    end
    
