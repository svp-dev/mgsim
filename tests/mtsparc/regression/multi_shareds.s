/*
  This test tests multi-register shareds used in a single instruction from an
  asynchronous component.
 */
    .file "multi_shareds.s"
    
    .globl main
    .align 64
main:
    clr %2
    allocate %2
    setlimit %2, 37
    set foo, %1
    crei %1, %2
    
    fputs %f0, %2, 0
    fputs %f0, %2, 1

    ! Sync
    sync %2, %1
    release %2
    mov %1, %0
    end

    .align 64
    .registers 0 0 0 0 2 2
foo:
    faddd %tdf0, %tdf0, %tlf0
    fmovs %tlf0, %tsf0; swch
    fmovs %tlf1, %tsf1
    end
