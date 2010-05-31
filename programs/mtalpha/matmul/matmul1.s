    .file "matmul1.s"
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    # Matrix width (only square matrices supported)
    .equ MAX_N, 10

#
# Multiply matrixA by matrixB and store result in matrixC. Single depth uTC version.
#
# $27 = main
# $10 = N
#
    .text
    .ent main
    .globl main
main:
    ldgp $29, 0($27)
    
	#	create (fam1; 0; N;)
	allocate $31, $4
	setlimit $4, $10
	swch
	cred    $4, thread1
	
	ldah    $0, A($29)      !gprelhigh
	lda     $0, A($0)       !gprellow
	putg    $0, $4, 0       # $g0 = A
	
	ldah    $1, B($29)      !gprelhigh
	lda     $1, B($1)       !gprellow
	putg    $1, $4, 1       # $g1 = B
	
	ldah    $2, C($29)      !gprelhigh
	lda     $2, C($2)       !gprellow
	putg    $2, $4, 2       # $g2 = C

	putg    $10, $4, 3      # $g3 = N

	#	sync(fam1);
	sync    $4, $0
	release $4
	mov     $0, $31
	end
	.end main


    .ent thread1
    # $g0 = A 
    # $g1 = B
    # $g2 = C
    # $g3 = N
    # $l0 = i
	.registers 4 0 8  0 0 0	    # GR,SR,LR, GF,SF,LF	
thread1:
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
L2e:cmplt   $l4, $g3, $l6
	bne     $l6, L2s
	swch
	
	s4addl  $l3, $l2, $l6       # $l6 = &C[i*N+j]
	stl     $l5, 0($l6)         # C[i*N+j] = sum
    
    #
    # }
    #
	addl    $l3, 1, $l3
L1e:cmplt   $l3, $g3, $l6
	bne     $l6, L1s
	end
	.end thread1

#
# Matrix data
#
    .data
    .align 6;
C:  .skip MAX_N * MAX_N * 4

    .section .rodata
    .align 6
A:  .skip MAX_N * MAX_N * 4
    .align 6
B:  .skip MAX_N * MAX_N * 4

