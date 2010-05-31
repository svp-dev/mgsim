    .file "matmul3.s"
    .set noat

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
    ldgp $29, 0($27)
    
	allocate $31, $4
	
	#	create (fam1; 0; N;)
	setlimit $4, $10
	swch
	.ifdef BLOCK1
	setblock $4, BLOCK1
	.endif
	cred $4, thread1
	
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
	.registers 4 0 5  0 0 0	    # GR,SR,LR, GF,SF,LF
thread1:
	allocate $31, $l4
	
	setlimit $l4, $g3
	swch
	.ifdef BLOCK2
	setblock $l4, BLOCK2
	.endif
	cred $l4, thread2
	
	putg    $g1, $l4, 1         # $g1 = B
	putg    $g3, $l4, 3         # $g3 = N

	mull    $l0, $g3, $l0       # $l0 = i*N
	s4addl  $l0, $g2, $l2 
	putg    $l2, $l4, 2         # $g2 = &C[i*N]
	
	s4addl  $l0, $g0, $l0
	putg    $l0, $l4, 0         # $g0 = &A[i*N]
	
	sync    $l4, $l0
	release $l4
	mov     $l0, $31
	end
	.end thread1


    .ent thread2
    # $g0 = &A[i*N]
    # $g1 = B
    # $g2 = &C[i*N]
    # $g3 = N
    # $l0 = j
	.registers 4 0 3  0 0 0	    # GR,SR,LR, GF,SF,LF
thread2:
    allocate $31, $l2
    setlimit $l2, $g3
    swch
    cred $l2, thread3
    
    putg    $g0, $l2, 0         # {$g0} = &A[i*N]
	s4addl  $l0, $g1, $l1
    putg    $l1, $l2, 1         # {$g1} = &B[j]
    putg    $g3, $l2, 2         # {$g2} = N
    puts    $31, $l2, 0         # {$d0} = sum = 0
    
	s4addl  $l0, $g2, $l1       # $l5 = &C[i*N+j]
	
    sync    $l2, $l0
    mov     $l0, $31
    swch
    gets    $l2, 0, $l0         # $l0 = {$s0}
    release $l2
	stl     $l0, 0($l1)         # C[i*N+j] = sum
	end
	.end thread2
	
	
	.ent thread3
	# $g0 = &A[i*N]
	# $g1 = &B[j]
	# $g2 = N
	# $s0 = sum
	# $l0 = k
	.registers 3 1 2  0 0 0	    # GR,SR,LR, GF,SF,LF
thread3:
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
