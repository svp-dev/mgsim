    .file "sac_flat.s"
    .arch ev6
    .text
    
    .equ MAX_X, 1024
    .equ MAX_Y, 1024
    
#
# Main Thread
#
# $27 = address of main
# $10 = X
# $11 = Y
#
    .globl main
    .ent main
main:
    ldah    $29, 0($27)     !gpdisp!1
    lda     $29, 0($29)     !gpdisp!1
    
    # Load adesc
    ldah    $0, adesc($29)  !gprelhigh
    lda     $0, adesc($0)   !gprellow   # $0 = adesc

    # Initialize the array descriptor
    mov       2,  $2
    stl      $2,   4($0)    # adesc[1] = 2;
    mull    $10, $11, $2
    stl      $2,   8($0)    # adesc[2] = x*y;
    stl     $10, 12($0)     # adesc[3] = x;
    stl     $11, 16($0)     # adesc[4] = y;

    # Load a
    ldah    $1, a($29)      !gprelhigh
    lda     $1, a($1)       !gprellow   # $1 = a

    # Create family
    clr      $3
    allocate $3, 0, 0, 0, 0
    setlimit $3, $2; swch
    cred    $3, fun
    mov     $3, $31         # Sync
    end

 /*   
    # Call print
    clr      $3
    allocate $3, 0, 0, 0, 0
    cred    $3, print
    mov     $3, $31         # Sync
    end
 */
    .end main
    
#
# fun thread
#
# $g0 = adesc
# $g1 = a
# $l0 = i
#
    .globl fun
    .ent fun
fun:
    .registers 2 0 4 0 0 5
    s8addl  $l0, $g1, $l1   # $l1 = &a[offset]
    ldt     $f1, 0($l1)     # $f1 = a[offset]
    cvttq   $f1, $f1; swch
    ftoit   $f1, $l2        # $l2 = stop = (int)a[offset]
    fclr    $f0             # $f0 = res = 0.0

    mov       2, $l3
    itoft   $l3, $f3
    cvtqt   $f3, $f3        # $f3 = 2.0
    mov       4, $l3
    itoft   $l3, $f4
    cvtqt   $f4, $f4        # $f4 = 4.0
    mov       1, $l3        # $l3 = i = 1
    br      2f; swch

1:
    itoft   $l3, $f1
    cvtqt   $f1, $f1        # $f1 = (double)i
    addt    $f1, $f3, $f2   # $f2 = (double)i + 2.0
    divt    $f4, $f1, $f1
    divt    $f4, $f2, $f2; swch
    subt    $f1, $f2, $f1; swch
    addt    $f0, $f1, $f0; swch
    
    addl    $l3, 4, $l3
2:  subl    $l3, $l2, $l0
    blt     $l0, 1b; swch
    
    stt     $f0, 0($l1)     # a[offset] = res
    end
    .end fun
    
    .section .bss
    .align 6
adesc: .skip 5 * 4
    
    .data
    .align 6
a:
