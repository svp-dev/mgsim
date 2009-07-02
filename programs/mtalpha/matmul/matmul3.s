    .file "matmul3.s"

    .section .rodata
    .ascii "\0TEST_INPUTS:R10:4 7 10\0"

    # Matrix width (only square matrices supported)
    .equ MAX_N,    16
    
    # Block sizes, comment lines to not set the block size
    .equ BLOCK1,    1
    .equ BLOCK2,    5

#
# Multiply A by B and store result in C. Triple depth microthreaded version.
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
    
	allocate $4, 0, 0, 0, 0
	
	ldah $0, A($29)     !gprelhigh
	lda  $0, A($0)      !gprellow
	ldah $1, B($29)     !gprelhigh
	lda  $1, B($1)      !gprellow
	ldah $2, C($29)     !gprelhigh
	lda  $2, C($2)      !gprellow
	mov  $10, $3

	#	create (fam1; 0; N;)
	setlimit $4, $10
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
	
	setlimit $l4, $g3
	swch
	.ifdef BLOCK2
	setblock $l4, BLOCK2
	.endif
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
    allocate $l4, 0, 0, 0, 0

	s4addl  $l0, $g2, $l5       # $l5 = &C[i*N+j]
	s4addl  $l0, $g1, $l1       # $l1 = &B[j]
    mov     $g0, $l0            # $l0 = &A[i*N]
	mov     $g3, $l2            # $l2 = N
	clr     $l3                 # $l3 = sum = 0

    setlimit $l4, $g3
    swch
    cred $l4, thread3
    
    mov     $l4, $31
    swch
    
	stl     $l3, 0($l5)         # C[i*N+j] = sum
	end
	.end thread2
	
	
	.ent thread3
	# $g0 = &A[i*N]
	# $g1 = &B[j]
	# $g2 = N
	# $s0 = sum
	# $l0 = k
thread3:
	.registers 3 1 2  0 0 0	    # GR,SR,LR, GF,SF,LF
    s4addl  $l0, $g0, $l1       # $l1 = &A[i*N+k]
    ldl     $l1, 0($l1)         # $l1 =  A[i*N+k]
    mull    $l0, $g2, $l0       # $l0 =  k*N
    s4addl  $l0, $g1, $l0       # $l0 = &B[k*N+j]
    ldl     $l0, 0($l0)         # $l0 =  B[k*N+j]
    mull    $l1, $l0, $l0
    swch
    addl    $d0, $l0, $s0
	end
	.end thread3

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
