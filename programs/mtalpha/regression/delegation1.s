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
    
    allocate (1 << 4) | (1 << 3) | (3 << 1), $2     # PID:1, Delegated,Suspend
    setlimit $2, 4
    cred $2, bar
    
    putg    42, $2, 0
    puts    2,  $2, 0
    
    # Sync 
    sync    $2, $0
    mov     $0, $31
    gets    $2, 0, $3
    release $2
    end
    .end main

    .ent bar
    .registers 1 1 1 0 0 0
bar:
    addq $d0, $g0, $l0
    swch
    print $l0, 0
    mov   $l0, $s0
    end
    .end bar

    .data
    .ascii "PLACES: 1,{1,2,3,4}\0"
