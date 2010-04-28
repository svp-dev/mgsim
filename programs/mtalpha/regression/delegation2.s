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
    allocate (1 << 4) | (1 << 3) | (3 << 1), $2     # PID:1, Delegated, Suspend
    setlimit $2, 4
    swch
    cred $2, bar
    
    putg 42, $2, 0
    swch
    
    # Sync 
    sync    $2, $0
    release $2
    mov     $0, $31
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

