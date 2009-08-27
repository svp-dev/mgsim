    .file "matmul2.s"

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
    ldah $29, 0($27)    !gpdisp!1
    lda  $29, 0($29)    !gpdisp!1
    
    clr      $4
	allocate $4, 0, 0, 0, 0
	
	ldah $0, A($29)     !gprelhigh
	lda  $0, A($0)      !gprellow
	ldah $1, B($29)     !gprelhigh
	lda  $1, B($1)      !gprellow
	ldah $2, C($29)     !gprelhigh
	lda  $2, C($2)      !gprellow
	mov  $10, $3

	# create (fam1; 0; N;)
	setlimit $4, $10
	swch
	.ifdef BLOCK1
	setblock $4, BLOCK1
	.endif
	cred $4, thread1
	
	# sync(fam1);
	mov $4, $31
	end
	.end main


    .ent thread1
    # $g0 = A 
    # $g1 = B
    # $g2 = C
    # $g3 = N
    # $l0 = i
thread1:
	.registers 4 0 5  0 0 0	    # GR,SR,LR, GF,SF,LF

    clr      $l4
	allocate $l4, 0, 0, 0, 0
	
	mull    $l0, $g3, $l0;      # $l0 = i*N
	s4addl  $l0, $g2, $l2;      # $l2 = &C[i*N]
	s4addl  $l0, $g0, $l0;      # $l0 = &A[i*N]
	mov     $g1, $l1
	mov     $g3, $l3
	
	setlimit $l4, $g3
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
