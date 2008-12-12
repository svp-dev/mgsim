#
# Livermore kernel 2 -- ICCG (Incomplete Cholesky Conjugate Gradient)
#
#    int ipntp = 0;
#    for (int m = M; m > 0; m--)
#    {
#        int ii   = 1 << m;
#        int ipnt = ipntp;
#        ipntp    = ipntp + ii;
#
#        for (int i = 1; i < ii; i+=2)
#        {
#            int k = ipnt + i;
#            x[ipntp + i / 2 + 1] = x[k] - (v[k]*x[k-1] + v[k+1]*x[k+1]);
#        }
#    }
#
    .file "l2_iccq.s"
    .text
    
    .equ MAX_M, 16
    .equ MAX_N, 1 << 16

#
# Main thread
#
# $27 = address of main
# $10 = M (= log2 N)
#
    .global main
    .ent main
main:
    ldah    $29, 0($27)     !gpdisp!1
    lda     $29, 0($29)     !gpdisp!1   # $29 = GP
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow   # $0 = X
    ldah    $1, V($29)      !gprelhigh
    lda     $1, V($1)       !gprellow   # $1 = V
    
    mov     1, $2           # $2 = 1
    clr     $3              # $3 = 0
    
    negq    1, $5
    allocate $4
    setstart $4, $10
    setlimit $4, 0
    setstep  $4, $5
    cred    $4, outer
    mov     $4, $31
    end
    .end main

#
# Outer thread
#
# $g0 = X
# $g1 = V
# $g2 = 1
# $d0 = ipntp
# $l0 = m
#
    .globl outer
    .ent outer
    .registers 3 1 5 0 0 0
outer:
    allocate $l4

    mov     $g1, $l1            # $l1 = V
    mov     $d0, $l3            # $l3 = ipnt
    sll     $g2, $l0, $l0       # $l0 = ii
    adll    $l0, 1, $l2
    setlimit $l4, $l2; swch
    setstart $l4, 1
    setstep  $l4, 2
    addq    $d0, $l0, $l2; swch # $l2 = ipntp + ii
    mov     $g0, $l0            # $l0 = X
    
    cred    $l4, inner
    mov     $l4, $31; swch
    mov     $l2, $s0            # $s0 = ipntp + ii
    end
    .end outer

#
# Inner thread
#
# $g0 = X
# $g1 = V
# $g2 = ipntp
# $g3 = ipnt
# $l0 = i
#
    .global inner
    .ent inner
    .registers 4 0 7 0 0 0
inner:
    addl    $g3, $l0, $l6   # $l6 = k = ipnt + i
    s4addq  $l6, $g1, $l2   # $l2 = &V[k]
    ldl     $l1, 0($l2)     # $l1 = V[k]
    s4addq  $l6, $g0, $l5   # $l5 = &X[k]
    ldl     $l4,-4($l5)     # $l4 = X[k-1]
    ldl     $l2, 4($l2)     # $l2 = V[k+1]
    ldl     $l3, 4($l5)     # $l3 = X[k+1]
    ldl     $l5, 0($l5)     # $l5 = X[k]
    srl     $l0,   1, $l0
    addq    $g2, $l0, $l0
    s4addq  $l0, $g0, $l0   # $l0 = &X[ipntp + i / 2]
    mull    $l1, $l4, $l1; swch
    mull    $l2, $l3, $l2; swch
    addq    $l1, $l2, $l1
    subq    $l5, $l1, $l1; swch
    stl     $l1, 4($l0)
    end
    .end inner

    .section .bss
   
    .align 6
X:  .skip MAX_N * 2
    
    .align 6
V:  .skip MAX_N * 2
