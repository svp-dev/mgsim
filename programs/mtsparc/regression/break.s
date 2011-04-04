/*
 This test does a break test.
 */
    .file "break.s"
    .text
    
    .globl main 
main:
    clr %2
    allocates 0, %2
    setlimit %2, 10
    setblock %2, 2
    swch;
    set      test, %3
    crei     %3, %2
    puts     %11,%2,0
    swch;
    sync     %2,%1
    mov      %1,%0
    gets     %2,0,%1
    detach   %2
    mov      %1,%0
    end;

    .registers 0 1 1 0 0 0   
test:
    subcc   %td0, 2, %tl0
    swch;
    bne     1f
    swch;
    break
1:
    mov     %tl0, %ts0
    end;
      
    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1 2 3 4 5 6 7 8 9 10\0"
