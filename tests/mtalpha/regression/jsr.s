/*
 This test does a break test.
 */
    .file "jsr.s"
    .set noat
    .arch ev6
    .text
    
    .ent main
    .globl main 
main:
    bsr $3,test1
    nop
    nop
    pal1d 0
    .end main

    .ent test
test1:
    ldpc $27
    ldgp $gp,0($27)
    lda $1,test2
    jsr $2,($1)
    nop
    nop
    pal1d 0
    .end test

    .ent test2
test2:
    nop
    end 
    .end test2
      
