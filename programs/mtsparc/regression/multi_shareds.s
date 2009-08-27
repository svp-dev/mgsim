/*
  This test tests multi-register shareds used in a single instruction from an
  asynchronous component.
 */
    .file "multi_shareds.s"
    
    .globl main
    .align 64
main:
    fmovs %f0, %f1
    fmovs %f0, %f2

    clr      %2
    allocate %2, 0, 0, 0, 0
    setlimit %2, 37
    cred foo, %2
    
    ! Sync
    mov %2, %0
    end

    .align 64
foo:
    .registers 0 0 0 0 2 0
    faddd %df0, %df0, %sf0
    end
