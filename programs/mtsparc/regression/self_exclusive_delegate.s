/*
 This test test exclusive delegation to the same core the delegation comes from.
 This should work properly because the delegated create should not register as an
 exclusive create on the source side.
 */
    .file "self_exclusive_delegate.s"
    
    .globl main
    .align 64
main:
    mov (0 << 1) | 1, %2    ! PID:0, Size:1
    allocatee %2, %2
    cred foo, %2
    
    ! Sync
    sync %2, %1
    release %2
    mov %1, %0
    end

    .align 64
    .registers 0 0 1 0 0 0
foo:
    print %l0, 0
    end

    .ascii "PLACES: 1\0"
