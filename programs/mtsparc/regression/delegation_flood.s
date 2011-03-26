/*
 This test does a delegation flood.
 It creates a family locally which all do a delegation to a specific core, the delegations
 that cannot immediately succeed should run locally.
 */
    .file "delegation_flood.s"
    .text

    .globl main
    .align 64
main:
    mov      1024, %2
    mov      (0 << 1) | 1, %1
    allocates %1
    setblock %1, 128
    setlimit %1, %2
    set      foo, %2
    crei     %2, %1
    sync     %1, %2
    release  %1
    mov      %2, %0
    end
    
    .align 64
    .registers 0 0 2 0 0 0
foo:
    mov (1 << 1) | 1, %tl0   ! PID:1, Size:1
    allocatex %tl0
    set      bar, %tl1
    crei     %tl1, %tl0
    sync     %tl0, %tl1
    release  %tl0
    mov      %tl1, %0
    end

    .align 64
    .registers 0 0 1 0 0 0
bar:
    ! Do some work to tie up the resource
    mov 32, %tl0
1:  dec %tl0
    cmp %tl0, 0
    bne 1b
    end

    .data
    .ascii "PLACES: 2\0"
