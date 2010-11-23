/*
 This test does a continuation test.
 It creates recursively a load of families that would, without continuations, cause deadlock.
 */
    .file "continuation.s"
    .text

    .align 64
    .globl main
main:
    allocates %0, %2        ! Default
    cred     bar, %2
    mov      255, %1
    putg     %1, %2, 0
    detach   %2
    end

    .align 64
    .registers 1 0 2 0 0 0
bar:
    ! Stop if %g0 is 0
    cmp      %g0, 0
    bne      1f
    nop
    end
1:    

    allocates %0, %l0        ! Default
    cred     bar, %l0
    swch
    sub      %g0, 1, %l1    ! Pass on %g0 - 1
    putg     %l1, %l0, 0
    swch
    
    ! Delay for %l1 loops to tie up resources
2:  cmp %l1, 0
    beq 3f
    dec %l1
    ba 2b
3:

    detach   %l0
    end

    .data
    .ascii "PLACES: 16\0"
