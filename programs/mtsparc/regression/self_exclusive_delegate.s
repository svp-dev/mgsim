/*
 This test test exclusive delegation to the same core the delegation comes from.
 This should work properly because the delegated create should not register as an
 exclusive create on the source side.
 */
    .file "self_exclusive_delegate.s"
    
    .globl main
    .align 64
main:
    allocate %2, 0, 0, 0, 0
    setplace %2, 5  ! Delegate to core #0, exclusive
    cred foo, %2
    
    ! Sync
    mov %2, %0
    end

    .align 64
foo:
    .registers 0 0 1 0 0 0
    print %l0, 0
    end

    .ascii "PLACES: 1\0"
