    #
    # Calculate fibonacci(N)
    #
    # Expects N (with N >= 2) in $10
    # Result ends up in $1
    #
    .file "fibo.s"
    .set noat
    .arch ev6
    .text
    
    .ent main
    .globl main
main:
    allocate $31, $3
    subl    $10, 1, $10
    setlimit $3, $10
    swch
    cred    $3, fibonacci
    mov     0, $0
    puts    $0, $3, 0
    mov     1, $1
    puts    $0, $3, 1
    sync    $3, $0
    mov     $0, $31
    gets    $3, 0, $0
    detach  $3
    mov     $0, $31
    end
    .end main

    .ent fibonacci
    .registers 0 2 0 0 0 0      # GR,SR,LR, GF,SF,LF
fibonacci:
    addl $d0, $d1, $s1
    swch
    mov $d1, $s0
    end
    .end fibonacci

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:2 5 7 12\0"
	
