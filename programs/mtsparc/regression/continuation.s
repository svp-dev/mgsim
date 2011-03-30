/*
 This test does a continuation test.
 It creates recursively a load of families that would, without continuations, cause deadlock.
 */
    .file "continuation.s"
    .text

    .align 64
    .globl main
main:
    clr %2
    allocates %2
    set      bar, %1
    crei     %1, %2
    mov      255, %1
    putg     %1, %2, 0
    release  %2
    end

    .align 64
    .registers 1 0 2 0 0 0
bar:
    ! Stop if %tg0 is 0
    cmp      %tg0, 0
    bne      1f
    nop
    end
1:
    clr %tl0
    allocates %tl0        ! Default
    set      bar, %tl1
    crei     %tl1, %tl0
    swch
    sub      %tg0, 1, %tl1    ! Pass on %tg0 - 1
    putg     %tl1, %tl0, 0
    swch
    
    ! Delay for %tl1 loops to tie up resources
2:  cmp %tl1, 0
    beq 3f
    dec %tl1
    ba 2b
3:

    release   %tl0
    end

    .data
    .ascii "PLACES: 16\0"
