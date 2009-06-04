/*
 This test case caused an infinite loop in the pipeline
 where the Waiting register was picked up from a later
 state in the pipeline, despite that the register in the
 register file was full.
 
 The nop after the print is required to allow the memory
 stage to write to the register file (otherwise the pipeline
 would get continuous priority and the memory would simply
 wake up all suspended threads).
*/
    .file "inf_pipeline_wait_loop.s"
    
    .globl main
    .ent main
main:
    ldgp $29, 0($27)
    
    setempty $0
    allocate $2, 0, 0, 0, 0
    setlimit $2, 250
    cred $2, foo
    
    ldah $5, X($29) !gprelhigh
    lda  $5, X($5)  !gprellow
    ldl $0, 0($5)
    
    # Sync
    mov $2, $31
    end
    .end main

    .ent foo
foo:
    .registers 1 0 0 0 0 0
    print $g0, 0
    nop
    end
    .end foo

    .data
X:
    .int 42
