/*
 This test does a concurrent break test.
 Multiple threads issue a break.
 */
    .file "conc_break.s"
    .text
    
    .globl main 
main:
    mov (8 << 1) | 8, %2
    allocates 0, %2
    setlimit %2,16
    swch;
    set      test, %3
    crei     %3, %2
    sync     %2,%1
    release  %2
    mov      %1,%0
    end;

    .align 64
    .registers 0 0 0 0 0 0   
test:
    nop
    nop
    nop
    nop
    break
    end;
