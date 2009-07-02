/*
    Livermore kernel 2 -- ICCG (Incomplete Cholesky Conjugate Gradient)

    double x[1001], v[1001];
    
    int ipntp = 0;
    for (int m = log2(n); m >= 0; m--)
    {
        int ii   = 1 << m;
        int ipnt = ipntp;
        ipntp    = ipntp + ii;

        for (int i = 1; i < ii; i+=2)
        {
            int k = ipnt + i;
            x[ipntp + i / 2] = x[k] - (v[k]*x[k-1] + v[k+1]*x[k+1]);
        }
    }
*/
    .file "l2_iccg.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:8\0"

    .text   
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
    allocate $4, 0, 0, 0, 0
    setstart $4, $10
    setlimit $4, $5
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
    .ent outer
    .registers 3 1 5 0 0 0
outer:
    allocate $l4, 0, 0, 0, 0
    mov      $g1, $l1            # $l1 = V
    sll      $g2, $l0, $l2       # $l2 = 1 << m = ii
    setlimit $l4, $l2; swch
    setstart $l4, 1
    setstep  $l4, 2
    mov      $g0, $l0            # $l0 = X
    addq     $d0, $l2, $l2; swch # $l2 = ipntp
    cred     $l4, inner
    mov      $d0, $l3            # $l3 = ipnt
    mov      $l4, $31; swch
    mov      $l2, $s0            # $s0 = ipntp
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
    .ent inner
    .registers 4 0 3 0 0 5
inner:
    print   $l0, 0
    addl    $g3, $l0, $l2   # $l2 = k = ipnt + i
    s8addq  $l2, $g1, $l1   # $l1 = &V[k]
    ldt     $lf1, 0($l1)    # $lf1 = V[k]
    s8addq  $l2, $g0, $l2   # $l2 = &X[k]
    ldt     $lf4,-8($l2)    # $lf4 = X[k-1]
    ldt     $lf2, 8($l1)    # $lf2 = V[k+1]
    ldt     $lf3, 8($l2)    # $lf3 = X[k+1]
    ldt     $lf0, 0($l2)    # $lf0 = X[k]
    srl     $l0,   1, $l0
    addq    $g2, $l0, $l0
    s8addq  $l0, $g0, $l0   # $l0 = &X[ipntp + i / 2]
    mult    $lf1, $lf4, $lf1; swch
    mult    $lf2, $lf3, $lf2; swch
    addt    $lf1, $lf2, $lf1; swch
    subt    $lf0, $lf1, $lf1; swch
    stt     $lf1, 0($l0)
    end
    .end inner

    .section .bss   
    .align 6
X:  .skip 1001 * 8
    .align 6
V:  .skip 1001 * 8
