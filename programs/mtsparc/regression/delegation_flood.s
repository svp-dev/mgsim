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
    allocate 2, %1      ! Local
    setblock %1, 128
    setlimit %1, %2
    cred     foo, %1
    sync     %1, %2
    release  %1
    mov      %2, %0
    end
    
    .align 64
    .registers 0 0 2 0 0 0
foo:
    allocate (1 << 4) | (1 << 3) | (3 << 1), %l0    ! PID:1, Suspend, Delegate, Exclusive
    cred     bar, %l0
    sync     %l0, %l1
    release  %l0
    mov      %l1, %0
    end

    .align 64
    .registers 0 0 1 0 0 0
bar:
    ! Do some work to tie up the resource
    mov 32, %l0
1:  dec %l0
    cmp %l0, 0
    bne 1b
    end

    .data
    .ascii "PLACES: 1,1\0"
