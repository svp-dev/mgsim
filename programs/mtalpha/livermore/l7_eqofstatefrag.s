/*
    Livermore kernel 7 -- Equation of state fragment

    double u[1001], x[1001], y[1001], z[1001];

    for(int i = 0; i < N; i++) {
        x[i] = u[i+0] + R * (z[i+0] + R * y[i+0]) +
          T * (u[i+3] + R * (u[i+2] + R * u[i+1]) +
          T * (u[i+6] + Q * (u[i+5] + Q * u[i+4])));
    }
*/
    .file "l7_eqofstatefrag.s"
    .arch ev6

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:512\0"
	
    .text    
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
    
    mov     10, $0; itoft $0, $f0; cvtqt $f0, $f0; # $f0 = Q
    mov     20, $0; itoft $0, $f1; cvtqt $f1, $f1; # $f1 = R
    mov     40, $0; itoft $0, $f2; cvtqt $f2, $f2; # $f2 = T
    
    ldah    $0, X($29)      !gprelhigh
    lda     $0, X($0)       !gprellow   # $0 = X
    ldah    $1, Y($29)      !gprelhigh
    lda     $1, Y($1)       !gprellow   # $1 = Y
    ldah    $2, Z($29)      !gprelhigh
    lda     $2, Z($2)       !gprellow   # $2 = Z
    ldah    $3, U($29)      !gprelhigh
    lda     $3, U($3)       !gprellow   # $3 = U
    
    clr      $4
    allocate $4, 0, 0, 0, 0
    setlimit $4, $10
    cred $4, loop
    mov $4, $31
    end
    .end main

#
# Loop thread
# $g0  = X
# $g1  = Y
# $g2  = Z
# $g3  = U
# $gf0 = Q
# $gf1 = R
# $gf2 = T
# $l0  = i
#
    .ent loop
    .registers 4 0 2  3 0 9
loop:
    s8addq  $l0, $g3, $l1   # $lf1 = &u[i]
    ldt     $lf2, 32($l1)   # $lf2 = u[i+4]
    ldt     $lf3, 40($l1)   # $lf3 = u[i+5]   
    ldt     $lf4, 48($l1)   # $lf4 = u[i+6]
    ldt     $lf5,  8($l1)   # $lf5 = u[i+1]
    ldt     $lf6, 16($l1)   # $lf6 = u[i+2]
    ldt     $lf7, 24($l1)   # $lf7 = u[i+3]
    ldt     $lf1,  0($l1)   # $lf1 = u[i+0]
    s8addq  $l0, $g1, $l1
    ldt     $lf8, 0($l1)    # $lf8 = y[i]
    s8addq  $l0, $g2, $l1
    ldt     $lf0, 0($l1)    # $lf0 = z[i]
    s8addq  $l0, $g0, $l0   # $l0 = &x[i]

    mult    $lf2, $gf0, $lf2; swch
    addt    $lf2, $lf3, $lf2; swch
    mult    $lf2, $gf0, $lf2; swch
    addt    $lf2, $lf4, $lf2; swch
    mult    $lf2, $gf2, $lf2; swch
    
    mult    $lf5, $gf1, $lf5; swch
    addt    $lf5, $lf6, $lf5; swch
    mult    $lf5, $gf1, $lf5; swch
    addt    $lf5, $lf2, $lf2; swch
    addt    $lf2, $lf7, $lf2; swch
    mult    $lf2, $gf2, $lf2; swch
    
    mult    $lf8, $gf1, $lf8; swch
    addt    $lf8, $lf0, $lf8; swch
    mult    $lf8, $gf1, $lf8; swch
    addt    $lf8, $lf2, $lf2; swch
    addt    $lf2, $lf1, $lf1; swch
    
    stt     $lf1, 0($l0)     # x[i] = result
    end
    .end loop


    .section .bss
    .align 6
X:  .skip 1001 * 8
    .align 6
Y:  .skip 1001 * 8
    .align 6
Z:  .skip 1001 * 8
    .align 6
U:  .skip 1001 * 8
