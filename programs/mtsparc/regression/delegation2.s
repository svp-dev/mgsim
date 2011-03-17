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
    
    allocateng (1 << 4) | (1 << 3) | (3 << 1), %3     ! PID:1, Delegated, Suspend
    setlimitng %3, 4
    cred bar, %3
    
    putg %1, %3, 0
    
    ! Sync
    sync %3, %1
    release %3
    mov %1, %0
    end

    .align 64
    .registers 1 0 1 0 0 0
bar:
    cmp %tl0, 0
    bne 1f
    swch
    print %tg0, 0
1:  nop
    end

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"

