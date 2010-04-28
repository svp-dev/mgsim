    !
    ! Calculate fibonacci(N)
    !
    ! Expects N (with N >= 2) in %11
    ! Result ends up in %2
    !
    .file "fibo.s"
    .text
    
    .globl main
main:
    ! First we get a free entry in the Family Table.
    ! Will write back asynchronously if there is no free entry.
    allocate %0, %4
    
    ! Get N - 1 as the loop limit
    sub     %11, 1, %11

    ! Set the loop limit on the allocated FT entry
    ! We tag this instruction with SWCH because %4 is the result
    ! of a possibly long latency operation (allocate)
    setlimit %4, %11
    swch
    
    ! Here we issue a direct (label operand) create with the
    ! allocated FT entry.
    cred    fibonacci, %4
    
    ! Write 0 and 1 into the first two locals (%0 is Read As Zero)
    mov      0, %1
    puts    %1, %4, 0
    mov      1, %1
    puts    %1, %4, 1
    
    ! This is the sync, we just read %1 and ignore it
    ! We tag it with SWCH because %1 is a long-latency result
    sync    %4, %1
    mov     %1, %0
    swch
    
    ! Get the final value
    gets    %4, 0, %1
    release %4

    ! Print the final value
    ! We tag it with END because it's the last instruction
    print   %1, %0
    end

    ! This thread uses 2 shared integers, nothing more
    .registers 0 2 0 0 0 0    
fibonacci:
    ! We add the first two dependents into the second shared
    ! Tagged with a SWCH because %d0 and %d1 are both not
    ! guaranteed to be there at time of execution.
    add %d0, %d1, %s1
    swch
    
    ! We copy the second dependent into the first shared.
    ! We tag it with END because it's the last instruction
    mov %d1, %s0
    end

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:2 5 7 12\0"

