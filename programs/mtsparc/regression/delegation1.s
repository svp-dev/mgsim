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
    
    mov (4 << 1) | 4, %3
    allocates %3, %3         ! PID:4, Size:4
    setlimit %3, 4
    cred bar, %3

    mov 42, %1
    mov  2, %2
    putg %1, %3, 0
    puts %2, %3, 0
    
    ! Sync
    sync %3, %1
    release %3
    mov %1, %0
    end

    .align 64
    .registers 1 1 1 0 0 0
bar:
    add %d0, %g0, %l0
    swch
    mov %l0, %s0
    print %l0, 0
    end

    .data
    .ascii "PLACES: 16\0"
        
