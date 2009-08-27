/*
 This test does a simple delegation test.
 It creates a family of 4 threads to the next ring, using a global and a shared.
 All threads use the shared and global.
 */
    .file "delegation1.s"
    .text

    .globl main
    .ent main
main:
    mov 42, $0
    mov  2, $1
    
    mov      (1 << 3) | (2 << 1), $2
    allocate $2, 0, 0, 0, 0
    setlimit $2, 4
    cred $2, bar
    
    # Sync 
    mov $2, $31
    end
    .end main

    .ent bar
    .registers 1 1 1 0 0 0
bar:
    addq $d0, $g0, $s0
    swch
    print $s0, 0
    end
    .end bar

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"
