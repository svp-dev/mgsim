/*
 This test does a simple delegation test.
 It creates a family of 4 threads to the next ring, using a global and a shared.
 All threads use the shared and global.
 */
    .file "delegation1.s"
    .text

    .align 64
    .globl main
main:
    mov 42, %1
    mov  2, %2
    
    allocate %3, 0, 0, 0, 0
    setlimit %3, 4
    setplace %3, (1 << 3) | (2 << 1)
    cred bar, %3
    
    ! Sync
    mov %3, %0
    end

    .align 64
    .registers 1 1 1 0 0 0
bar:
    add %d0, %g0, %s0
    swch
    print %s0, 0
    end

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"
        
