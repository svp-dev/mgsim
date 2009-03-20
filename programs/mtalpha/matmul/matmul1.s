    .file "matmul1.s"

    # Matrix width (only square matrices supported)
    .equ N, 10

#
# Multiply matrixA by matrixB and store result in matrixC. Single depth uTC version.
#
    .text
    .ent main
    .globl main
main:
    ldah $29, 0($27)    !gpdisp!1
    lda  $29, 0($29)    !gpdisp!1
    
	allocate $4, 0, 0, 0, 0

	ldah $0, matrixA($29)      !gprelhigh
	lda  $0, matrixA( $0)      !gprellow
	ldah $1, matrixB($29)      !gprelhigh
	lda  $1, matrixB( $1)      !gprellow
	ldah $2, matrixC($29)      !gprelhigh
	lda  $2, matrixC( $2)      !gprellow
	mov  N,  $3

	#	create (fam1; 0; N;)
	setlimit $4, N
	swch
	cred $4, thread1
	
	#	sync(fam1);
	mov $4, $31
	end
	.end main


    .ent thread1
    # $g0 = matrixA 
    # $g1 = matrixB
    # $g2 = matrixC
    # $g3 = N
    # $l0 = i
thread1:
	.registers 4 0 8  0 0 0	    # GR,SR,LR, GF,SF,LF
	
	mull    $l0, $g3, $l0;      # $l0 = i*N
	s4addl  $l0, $g2, $l2;      # $l2 = &C[i*N]
	s4addl  $l0, $g0, $l0;      # $l0 = &A[i*N]
	
    #
    # for (int j = 0; j < N; j++) {
    #
	clr $l3                     # $l3 = j
	br L1e
	swch
L1s:

	clr     $l5                 # $l5 = sum = 0
	s4addl  $l3, $g1, $l1       # $l1 = &B[j]

    #
	# for (int k = 0; k < N; k++) {
	#
    clr $l4                     # $l4 = k
    br L2e
    swch
L2s:

    s4addl  $l4, $l0, $l6       # $l6 = &A[i*N+k]
    ldl     $l6, 0($l6)         # $l6 =  A[i*N+k]
    ldl     $l7, 0($l1)         # $l7 =  B[k*N+j]
    s4addl  $g3, $l1, $l1       # $l1 = &B[k*N+j]
    
    mull    $l6, $l7, $l6       # $l6 = A[i*N+k] * B[k*N+j]
    swch
    
    addl    $l5, $l6, $l5       # $l5 = sum = sum + A[i*N+k] * B[k*N+j]
    
    #
    # }
    #
    addl    $l4, 1, $l4
L2e:cmplt   $l4, N, $l6
	bne     $l6, L2s
	swch
	
	s4addl  $l3, $l2, $l6       # $l6 = &C[i*N+j]
	stl     $l5, 0($l6)         # C[i*N+j] = sum
    
    #
    # }
    #
	addl    $l3, 1, $l3
L1e:cmplt   $l3, N, $l6
	bne     $l6, L1s
	end
	.end thread1

#
# Matrix data
#
    .data
    .align 6;
    .globl matrixC
matrixC:
    .skip N*N*4

    .section .rodata

    .align 6
matrixA:
    .rep N*N
    .int 2
    .endr

    .align 6
matrixB:
    .rep N*N
    .int 3
    .endr

