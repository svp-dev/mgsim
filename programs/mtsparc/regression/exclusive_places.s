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

! %ts0/%td0 = accumulator
    .align 64
    .registers 0 1 2 0 0 0
foo:
    mov (1 << 1) | 1, %tl1
    allocatee %tl1, %tl1      ! PID:1, Size:1, Exclusive
    swch
    cred     bar, %tl1
    sync     %tl1, %tl0
    mov      %tl0, %0; swch
    gets     %tl1, 0, %tl0
    release  %tl1
    add      %td0, %tl0, %ts0
    end
    
! %ts0 = return value
    .align 64
    .registers 0 1 2 0 0 0
bar:
    set val, %tl0
    ld  [%tl0], %tl1
    add %tl1, 1, %tl1
    mov %tl1, %ts0
    st  %tl1, [%tl0]
    end

    .data
    .align 4
val:
    .int 0

    .ascii "PLACES: 16\0"
