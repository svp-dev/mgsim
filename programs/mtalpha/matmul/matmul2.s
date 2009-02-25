    .file "matmul2.s"

    # Matrix width (only square matrices supported)
    .equ N,         10
    
    # Block sizes, comment lines to not set the block size
    .equ BLOCK1,    5

#
# Multiply matrixA by matrixB and store result in matrixC. Double depth uTC version.
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
	.ifdef BLOCK1
	setblock $4, BLOCK1
	.endif
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
	.registers 4 0 5  0 0 0	    # GR,SR,LR, GF,SF,LF

	allocate $l4, 0, 0, 0, 0
	
	mull    $l0, $g3, $l0;      # $l0 = i*N
	s4addl  $l0, $g2, $l2;      # $l2 = &C[i*N]
	s4addl  $l0, $g0, $l0;      # $l0 = &A[i*N]
	mov     $g1, $l1
	mov     $g3, $l3
	
	setlimit $l4, N
	swch
	cred $l4, thread2
	mov $l4, $31
	end
	.end thread1


    .ent thread2
    # $g0 = &A[i*N]
    # $g1 = B
    # $g2 = &C[i*N]
    # $g3 = N
    # $l0 = j
thread2:
	.registers 4 0 6  0 0 0	    # GR,SR,LR, GF,SF,LF

	s4addl  $l0, $g1, $l1       # $l1 = &B[j]
	clr     $l2                 # $l2 = sum = 0

    #
	# for (int k = 0; k < N; k++) {
	#
    clr $l3                     # $l3 = k
    br L2e
    swch
L2s:

    s4addl  $l3, $g0, $l4       # $l4 = &A[i*N+k]
    ldl     $l4, 0($l4)         # $l4 =  A[i*N+k]
    ldl     $l5, 0($l1)         # $l5 =  B[k*N+j]
    s4addl  $g3, $l1, $l1       # $l1 = &B[k*N+j]
    
    mull    $l4, $l5, $l4       # $l4 = A[i*N+k] * B[k*N+j]
    swch
    
    addl    $l2, $l4, $l2       # $l2 = sum = sum + A[i*N+k] * B[k*N+j]
    
    #
    # }
    #
    addl    $l3, 1, $l3
L2e:cmplt   $l3, N, $l4
	bne     $l4, L2s
	swch
	
	s4addl  $l0, $g2, $l0       # $l0 = &C[i*N+j]
	stl     $l2, 0($l0)         # C[i*N+j] = sum
	end
	.end thread2

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

