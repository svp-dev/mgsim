/*
 This test case caused an infinite loop in the pipeline
 where the Waiting register was picked up from a later
 state in the pipeline, despite that the register in the
 register file was full.
 
 The nop after the print is required to allow the memory
 stage to write to the register file (otherwise the pipeline
 would get continuous priority and the memory would simply
 wake up all suspended threads).
 
 
 Note: now that we use "putg" this scenario can no longer occur.
 No more than one thread can wait on a memory load.
 Maybe this regression should be deleted?
*/
    .file "inf_pipeline_wait_loop.s"
    .set noat
    
    .globl main
    .ent main
main:
    ldgp $29, 0($27)
    
    allocate $31, $2
    setlimit $2, 250
    cred $2, foo
    
    ldah $5, X($29) !gprelhigh
    lda  $5, X($5)  !gprellow
    ldl $0, 0($5)
    putg $0, $2, 0
    
    # Sync
    sync $2, $0
    release $2
    mov $0, $31
    end
    .end main

    .ent foo
    .registers 1 0 0 0 0 0
foo:
    print $g0, 0
    nop
    end
    .end foo

    .data
X:
    .int 42
