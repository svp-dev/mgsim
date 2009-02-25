    .file "matmul_o.s"
    
    # Maximum matrix width we support with this code
    .equ MAX_N, 128

    #
    # Multiply matrix A by matrix B and store result in matrix C.
    # Optimized uTC version.
    #
    # Expects log2(N) (N = matrix width; square matrices) in $10.
    # Expects the block size for the outer create in $11.
    #
    .text
    .ent main
    .globl main
main:
    ldah $29, 0($27)    !gpdisp!1
    lda  $29, 0($29)    !gpdisp!1
    
	allocate $6
	
	ldah $0, A($29)      !gprelhigh
	lda  $0, A( $0)      !gprellow
	ldah $1, B($29)      !gprelhigh
	lda  $1, B( $1)      !gprellow
	ldah $2, C($29)      !gprelhigh
	lda  $2, C( $2)      !gprellow

    mov        1, $3
    sll  $3, $10, $3          # $3 = N
    subl $3,   1, $4          # $4 = N - 1
    sll  $4,   2, $5          # $5 = (N - 1) * 4

    mull $3,  $3, $7
    subl $7,   1, $7
	setlimit $6,  $7          # limit = N * N - 1    
	swch
	setblock $6, $11
	cred $6, outer
	
	mov $6, $31
	end
	.end main


    .ent outer
    # $g0 = A 
    # $g1 = B
    # $g2 = C
    # $g3 = N
    # $g4 = N - 1
    # $g5 = (N - 1) * 4
    # $l0 = ij
outer:
	.registers 6 0 6  0 0 0	    # GR,SR,LR, GF,SF,LF
	allocate $l4
	
	s4addl $l0, $g2, $l5        # $l5 = &C[i][j]
	
	# Get i and j from ij.
	# Use the fact that N is a power of two to avoid integer division.
	and    $l0, $g4, $l1        # $l1 = (ij % N) = j
	subl   $l0, $l1, $l0        # $l0 = ij - j   = i * N
	s4addl $l0, $g0, $l0        # $l0 = &A[i][0]
	s4addl $l1, $g1, $l1        # $l1 = &B[0][j]
	mov    $g3, $l2             # $l2 = N
	clr    $l3                  # $l3 = 0 (s)
	
	setlimit $l4, $g5
	swch
	setstep  $l4, 4
	setplace $l4, 0
	cred $l4, inner
	mov $l4, $31
	swch
	
	stl $l3, 0($l5)              # C[i][j] = s
	
	end
	.end outer
	
	.ent inner
	# $g0 = &A[i][0]
	# $g1 = &B[0][j]
	# $g2 = N
	# $l0 = k * 4
inner:
    .registers 3 1 2 0 0 0      # GR,SR,LR, GF,SF,LF
    addl $l0, $g0, $l1          # $l1 = &A[i][k]
    ldl  $l1, 0($l1)
    mull $l0, $g2, $l0
    addl $l0, $g1, $l0          # $l0 = &B[k][j]
    ldl  $l0, 0($l0)
    mull $l0, $l1, $l0
    swch
    addl $d0, $l0, $s0
    end
    .end inner
    	
#
# Matrix data
#
    .data
    .align 6
C:  .skip MAX_N * MAX_N * 4

    .section .rodata
    .align 6
A:  .skip MAX_N * MAX_N * 4

    .align 6
B:  .skip MAX_N * MAX_N * 4

