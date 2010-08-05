/*
 This test does a break test.
 */
    .file "break.s"
    .set noat
    .arch ev6
    .text
    
    .ent main
    .globl main 
main:
    allocate (1 << 1), $2    # Group create
    setlimit $2,10
    setblock $2,2
    swch
    cred     $2, test
    puts     $10,$2,0
    sync     $2,$1
    mov      $1,$31
    gets     $2,0,$0
    detach   $2
    mov      $0,$1
    end 
    .end main

    .ent test
    .registers 0 1 1 0 0 0   
test:
    subl    $d0, 2, $l0
    swch
    bne     $l0, 1f
    swch
    break
1:
    mov     $l0, $s0
    end
    .end test
      
    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1 2 3 4 5 6 7 8 9 10\0"
