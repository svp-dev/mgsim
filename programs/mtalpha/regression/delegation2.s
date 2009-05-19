/*
 This test does a simple delegation test.
 It creates a family of 4 threads to the next ring, using a global.
 Only the first thread uses the global.
 */
    .file "delegation2.s"
    .text

    .globl main
    .ent main
main:
    mov 42, $0
    
    allocate $2, 0, 0, 0, 0
    setlimit $2, 4
    setplace $2, (1 << 3) | (2 << 1)
    cred $2, bar
    
    # Sync 
    mov $2, $31
    end
    .end main

    .ent bar
    .registers 1 0 1 0 0 0
bar:
    bne $l0, 1f
    swch
    print $g0, 0
1:  nop
    end
    .end bar

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"

