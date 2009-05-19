/*
 This test does a simple delegation test.
 It creates a family of 4 threads to the next ring, using a global and a shared.
 Only the first thread uses the global.
 */
    .file "delegation2.s"
    .text

    .align 64
    .globl main
main:
    mov 42, %1
    
    allocate %3, 0, 0, 0, 0
    setlimit %3, 4
    setplace %3, (1 << 3) | (2 << 1)
    cred bar, %3
    
    ! Sync
    mov %3, %0
    end

    .align 64
    .registers 1 0 1 0 0 0
bar:
    cmp %l0, 0
    bne 1f
    swch
    print %g0, 0
1:  nop
    end

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"

