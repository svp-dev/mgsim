    .file "exclusive_places.s"
    
    .globl main
    .align 64
main:
    mov 0, %1
    
    allocate %2, 0, 0, 0, 0
    setlimit %2, 64
    setplace %2, 2      ! Local
    cred foo, %2
    
    ! Sync
    mov %2, %0; swch

    ! Check if the result matches
    set 2080, %2
    print %1, 0
    cmp %1, %2
    beq 1f
    unimp
1:  nop
    end

! %s0/%d0 = accumulator
    .align 64
foo:
    .registers 0 1 3 0 0 0
    allocate %l1, 0, 0, 0, 0
    setplace %l1, 3; swch   ! Local exclusive
    cred     bar, %l1
    mov      %l1, %0; swch
    add      %d0, %l0, %s0
    end
    
! %s0 = return value
    .align 64
bar:
    .registers 0 1 2 0 0 0
    set val, %l0
    ld  [%l0], %l1
    add %l1, 1, %s0
    st  %s0, [%l0]
    end

    .data
    .align 4
val:
    .int 0
