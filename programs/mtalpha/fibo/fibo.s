    #
    # Calculate fibonacci(N)
    #
    # Expects N (with N >= 2) in $10
    # Result ends up in $1
    #
    .file "fibo.s"
    .arch ev6
    .text
    
    .ent main
    .globl main
main:
    clr      $3
    allocate $3, 0, 0, 0, 0
    mov     0, $0
    mov     1, $1
    subl    $10, 1, $10
    setlimit $3, $10
    swch
    cred    $3, fibonacci
    mov     $3, $31
    end
    .end main

    .ent fibonacci
fibonacci:
    .registers 0 2 0 0 0 0      # GR,SR,LR, GF,SF,LF
    addl $d0, $d1, $s1
    swch
    mov $d1, $s0
    end
    .end fibonacci

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:2 5 7 12\0"
	
