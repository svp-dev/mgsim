    .file "sac_nested_group.s"
    .set noat
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
    clr      $2
    allocate/s $2, 0, 0, 0, 0
    setlimit $2, $10; swch
    setblock $2, 1
    cred    $2, with_0_set_0
    mov     $2, $31         # Sync
    end

 /*   
    # Call print
    clr      $2
    allocate/s $2, 0, 0, 0, 0
    cred    $2, print
    mov     $2, $31         # Sync
    end
 */
    .end main
    
#
# with_0_set_0 thread
#
# $g0 = adesc
# $g1 = a
# $l0 = i
#
    .globl with_0_set_0
    .ent with_0_set_0
    .registers 2 0 5 0 0 0    
with_0_set_0:
    mov     $g0, $l1
    mov     $g1, $l2
    ldl     $l3, 16($g0)
    clr      $l4
    allocate/s $l4, 0, 0, 0, 0
    setlimit $l4, $l3; swch
    setblock $l4, 1
    cred    $l4, fun
    mov     $l4, $31            # Sync
    end
    .end with_0_set_0
    
#
# fun thread
#
# $g0 = i
# $g1 = adesc
# $g2 = a
# $l0 = j
#
    .globl fun
    .ent fun
    .registers 3 0 4 0 0 5
fun:
    ldl     $l1, 12($g1)
    mull    $l1, $g0, $l1
    addl    $l1, $l0, $l1   # $l1 = offset = i * adesc[3] + j
    s8addl  $l1, $g2, $l1   # $l1 = &a[offset]
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
a:  .skip MAX_X * MAX_Y * 4
