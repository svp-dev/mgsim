/*
 This test test exclusive delegation. A shared, global variable is incremented
 on an exclusive place and summed in its parent. Without exclusivity, the
 result would not be correct due to overlapping increments.
 */
    .file "exclusive_places.s"
    
    .globl main
    .align 64
main:
    allocate %0, %2
    setlimit %2, 64
    cred foo, %2
    
    puts %0, %2, 0
    
    ! Sync
    sync %2, %1
    mov %1, %0; swch
    gets %2, 0, %1
    release %2

    ! Check if the result matches
    set 2080, %2
    print %1, 0
    cmp %1, %2
    beq 1f
    unimp           ! Cause an invalid instruction
1:  nop
    end

! %s0/%d0 = accumulator
    .align 64
    .registers 0 1 2 0 0 0
foo:
    mov (1 << 1) | 1, %l1
    allocatee %l1, %l1      ! PID:1, Size:1, Exclusive
    swch
    cred     bar, %l1
    sync     %l1, %l0
    mov      %l0, %0; swch
    gets     %l1, 0, %l0
    release  %l1
    add      %d0, %l0, %s0
    end
    
! %s0 = return value
    .align 64
    .registers 0 1 2 0 0 0
bar:
    set val, %l0
    ld  [%l0], %l1
    add %l1, 1, %l1
    mov %l1, %s0
    st  %l1, [%l0]
    end

    .data
    .align 4
val:
    .int 0

    .ascii "PLACES: 16\0"
