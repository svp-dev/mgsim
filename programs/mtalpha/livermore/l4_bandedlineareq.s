# Livermore kernel 4 -- Banded linear equations
#
#    int m = (1001 - 7) / 2;
#
#    int x[N], Y[N];
#
#    for (int i = 6; i < 1001; i += m)
#    {
#        int temp = 0;
#        for(int j = 0; j < N/5; j++) {
#            temp += x[i + j - 6] * y[j * 5 + 4];
#        }
#
#        x[i - 1] = y[4] * (x[i - 1] - temp);
#    }
#
    .file "l4_bandedlineareq.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:1024\0"
	
    .text
    
    .equ MAX_N, 1048576
    .equ M,     (1001 - 7) / 2
    
#
# Main thread
#
# $27 = address of main
# $10 = N
#
    .globl main
    .ent main
main:
    ldah    $29, 0($27)     !gpdisp!1
    lda     $29, 0($29)     !gpdisp!1
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow   # $0 = X
    ldah    $1, Y($29)      !gprelhigh
    lda     $1, Y($1)       !gprellow   # $1 = Y
    
    #
    # Calculate N / 5
    #
    # http://blogs.msdn.com/devdev/archive/2005/12/12/502980.aspx
    #
    # N / 5
    # = (2**32 / 5)  * N / 2**32
    # = 858993459.25 * N / 2**32 (approximated, but correct)
    # = (N * 0x33333333 + (N >> 2)) >> 32
    #
    ldah    $2, 0x3333($31)
    lda     $2, 0x3333($2)
    mulq    $10, $2,  $2
    srl     $10,  2, $10
    addq    $10, $2,  $2
    srl     $2, 32,  $2     # $2 = N / 5
    
    lda     $4,    M($31)
    lda     $5, 1001($31)
    
    allocate $3, 0, 0, 0, 0
    setstart $3, 6
    setlimit $3, $5
    setstep  $3, $4
    cred    $3, outer
    mov     $3, $31
    end
    
    .end main
    
#
# Outer loop thread
#
# $g0 = X
# $g1 = Y
# $g2 = N/5
# $l0 = i
#
    .globl outer
    .ent outer
outer:
    .registers 3 0 8 0 0 0
    allocate $l4, 0, 0, 0, 0
    mov     $g0, $l1    # $l1 = X
    mov     $g1, $l2    # $l2 = Y
    clr     $l3         # $l3 = temp = 0
    setlimit $l4, $g2; swch
    cred    $l4, inner
    
    s4addq $l0, $g0, $l5    # $l5 = &X[i]
    ldl $l6, -4($l5)        # $l6 = X[i-1]
    ldl $l7, 16($g1)        # $l7 = Y[4]
    
    mov     $l4, $31; swch
    subl    $l6, $l3, $l6; swch   # $l6 = X[i-1] - temp
    mull    $l7, $l6, $l7; swch
    stl     $l7, -4($l5)
    end
    .end outer

#
# Inner loop thread
#
# $g0 = i
# $g1 = X
# $g2 = Y
# $d0 = temp
# $l0 = j
#
    .globl inner
    .ent inner
inner:
    .registers 3 1 3 0 0 0
    addq    $g0, $l0, $l1   # $l1 = i + j
    s4addq  $l1, $g1, $l1   # $l1 = &X[i + j];
    ldl     $l1, -24($l1)   # $l1 = X[i + j - 6];
    
    sll     $l0,   4, $l2
    addq    $l2, $l0, $l0   # $l0 = j * 5;
    s4addq  $l0, $g2, $l0   # $l0 = &Y[j * 5];
    ldl     $l0, 16($l0)    # $l0 = Y[j * 5 + 4];
    
    mull    $l0, $l1, $l0; swch
    addl    $d0, $l0, $s0
    end
    .end inner


    .section .bss
    .align 6
X:  .skip MAX_N * 4

    .align 6
Y:  .skip MAX_N * 4
