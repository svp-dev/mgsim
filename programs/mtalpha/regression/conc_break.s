/*
 This test does a concurrent break test.
 Multiple threads issue a break.
 */
    .file "conc_break.s"
    .set noat
    .arch ev6
    .text
    
    .ent main
    .globl main 
main:
    mov (8 << 1) | 8, $2
    allocate/s $2, 0, $2
    setlimit $2,16
    swch
    cred     $2, test
    sync     $2,$1
    release  $2
    mov      $1,$31
    end 
    .end main

    .ent test
    .registers 0 0 0 0 0 0   
test:
    nop
    nop
    nop
    nop
    break
    end
    .end test
