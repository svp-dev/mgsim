    .file "matmul2.s"
    .set noat

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    # Matrix width (only square matrices supported)
    .equ MAX_N,    10
    
    # Block sizes, comment lines to not set the block size
    .equ BLOCK1,    5

#
# Multiply A by B and store result in C. Double depth microthreaded version.
#
# $27 = main
# $10 = N
#
    .text
    .ent main
    .globl main
main:
    ldgp $29, 0($27)
    
	allocate/s (1 << 3), $4
	
	# create (fam1; 0; N;)
	setlimit $4, $10
	swch
	.ifdef BLOCK1
	setblock $4, BLOCK1
	.endif
	cred    $4, thread1
	
	ldah    $0, A($29)      !gprelhigh
	lda     $0, A($0)       !gprellow
	putg    $0, $4, 0       # $g0 = X

	ldah    $1, B($29)      !gprelhigh
	lda     $1, B($1)       !gprellow
	putg    $1, $4, 1       # $g1 = B

	ldah    $2, C($29)      !gprelhigh
	lda     $2, C($2)       !gprellow
	putg    $2, $4, 2       # $g2 = C

	putg    $10, $4, 3      # $g3 = N
	
	# sync(fam1);
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
	.registers 4 0 3  0 0 0	    # GR,SR,LR, GF,SF,LF
thread1:
	allocate/s (1 << 3), $l2
	setlimit $l2, $g3
	swch
	cred $l2, thread2
	
	putg    $g1, $l2, 1         # $g1 = B
	putg    $g3, $l2, 3         # $g3 = N

	mull    $l0, $g3, $l0;      # $l0 = i*N
	s4addl  $l0, $g2, $l1;
	putg    $l1, $l2, 2         # $g2 = &C[i*N]
	
	s4addl  $l0, $g0, $l0;
	putg    $l0, $l2, 0         # $g0 = &A[i*N]

	sync    $l2, $l0
	release $l2
	mov     $l0, $31
	end
	.end thread1


    .ent thread2
    # $g0 = &A[i*N]
    # $g1 = B
    # $g2 = &C[i*N]
    # $g3 = N
    # $l0 = j
	.registers 4 0 6  0 0 0	    # GR,SR,LR, GF,SF,LF
thread2:
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
L2e:cmplt   $l3, $g3, $l4
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
C:  .skip MAX_N * MAX_N * 4

    .section .rodata
    .align 6
A:  .skip MAX_N * MAX_N * 4
    .align 6
B:  .skip MAX_N * MAX_N * 4
