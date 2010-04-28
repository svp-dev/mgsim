/*
  This test tests multi-register shareds used in a single instruction from an
  asynchronous component.
 */
    .file "multi_shareds.s"
    
    .globl main
    .align 64
main:
    allocate %0, %2
    setlimit %2, 37
    cred foo, %2
    
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
    faddd %df0, %df0, %lf0
    fmovs %lf0, %sf0; swch
    fmovs %lf1, %sf1
    end
