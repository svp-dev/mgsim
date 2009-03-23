    #
    # Calculate fibonacci(N)
    #
    # Expects N (with N >= 2) in $2
    # Result ends up in $1
    #
    .file "fibo.s"
    .arch ev6
    .text
    
    .ent main
    .globl main
main:
    allocate $3, 0, 0, 0, 0
    mov     0, $0
    mov     1, $1
    subl    $2, 1, $2
    setlimit $3, $2
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
    .ascii "\0TEST_INPUTS:R2:2 5 7 12\0"
	