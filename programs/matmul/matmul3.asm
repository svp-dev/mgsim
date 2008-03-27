// matmul3.asm

N = 7; // matrix size (only square matrices supported)
BLOCK1 = 1; // block size for thread1
BLOCK2 = 2; // block size for thread2
BLOCK3 = 3; // block size for thread3

LOCAL = 0x8000;		// The created family must be a local family.
struct TCB
{
    quad startIndex;		// Start index of the family
    quad endIndex;		// End index of the family (inclusive)
    quad step;			// Step size
    quad pc;			// Initial Program Counter for new threads
    word blockSize;		// Family Block size
    word intRegsNo;		// Integer register counts (0LLL LLSS SSSG GGGG)
    word fltRegsNo;		// Float   register counts (0LLL LLSS SSSG GGGG)
    word misc;
};
matrixA = 0x100000;
matrixB = 0x200000;
matrixC = 0x1000;

/*
 * Multiply matrixA by matrixB and store result in matrixC. Full depth uTC version.
 */

main:
	// register allocation:
	// R0: misc.
	// R1: misc
	// R2: i
	// R3: N*N
	// R4: matrixA
	// R5: matrixB
/*
	//	for (i=0; i<N*N; i++)
	addl $R31, $R31, $R2;
	ldah $R3, (N*N) >> 16;
	lda $R3, (N*N) & 0xFFFF($R3);
	ldah $R4, (matrixA>>16)+((matrixA>>15)&1);
	lda $R4, matrixA & 0xFFFF($R4);
	ldah $R5, (matrixB>>16)+((matrixB>>15)&1);
	lda $R5, matrixB & 0xFFFF($R5);
L1:
	cmplt $R2, $R3, $R0;
	beq $R0, L2;
	//	{
	//			matrixA[i]=2;
	s4addl $R2, $R4, $R1;
	addl $R31, 2, $R0;
	stl $R0, ($R1);
	//			matrixB[i]=3;
	s4addl $R2, $R5, $R1;
	addl $R31, 3, $R0;
	stl $R0, ($R1);
	//	}
	addl $R2, 1, $R2;
	br $R31, L1;
L2:*/
	//	create (fam1; 0; N-1;)
	cre $R0, tcb1;
	//	sync(fam1);
	addl $R0, $R31, $R31;
	swch;
	addl $R31, $R31, $R31;
	end;

.align 64; // TCBs must be 64-byte aligned
TCB tcb1 = {
	0, N-1, 1, thread1, BLOCK1,
	0 | (0 << 5) | (2 << 10),	// $R: G,S,L
	0 | (0 << 5) | (0 << 10),	// $F: G,S,L
	0
};

thread1:
	.registers_int 0 0 2;		// G,S,L
	// register allocation:
	// LR0: misc
	// LR1: jN
	
	//		index int j; int jN;
	//		jN=j*N;
	mull $LR0, N, $LR0;
	//		create (fam2; 0; N-1;)
	cre $LR1, tcb2;
	//		sync(fam2);
	addl $LR1, $R31, $R31;
	end;

.align 64;
TCB tcb2 = {
	0, N-1, 1, thread2, BLOCK2,
	1 | (0 << 5) | (4 << 10),	// $R: G,S,L
	0 | (0 << 5) | (0 << 10),	// $F: G,S,L 
	0
};

thread2:
	.registers_int 1 0 4;		// G,S,L
	// register allocation:
	// GR0: jN
	// LR0: jN
	// LR1: i
	// LR2: v
	// LR3: misc.

	//			index int i;
	addl $LR0, $R31, $LR1;
	addl $GR0, $R31, $LR0;
	//	shared int v = 0;
	addl $R31, $R31, $LR2;
	//	create (fam3; 0; N-1;)
	cre $LR3, tcb3;
	//		sync(fam3);
	addl $LR3, $R31, $R31;
	swch;
	//	matrixC[jN+i] = v;
	addl $LR0, $LR1, $LR3;
	ldah $LR0, (matrixC>>16)+((matrixC>>15)&1);
	lda $LR0, matrixC & 0xFFFF($LR0);
	s4addl $LR3, $LR0, $LR0;
	stl $LR2, ($LR0);
	end;

.align 64;
TCB tcb3 = {
	0, N-1, 1, thread3, BLOCK3,
	2 | (1 << 5) | (4 << 10),	// $R: G,S,L
	0 | (0 << 5) | (0 << 10),	// $F: G,S,L
	0
};

thread3:
	.registers_int 2 1 4;		// G,S,L
	// register allocation:
	// GR0: jN
	// GR1: i
	// SR0: v
	// R0: misc.
	// R1: miscr
	// R2: k
	// R3: misc

	//	index int k;
	addl $LR0, $R31, $LR2;

	//	v+=matrixA[k*N+i]*matrixB[jN+k];
	ldah $LR1, (matrixA>>16)+((matrixA>>15)&1);
	lda $LR1, matrixA & 0xFFFF($LR1);
	mull $LR2, N, $LR0;
	addl $LR0, $GR1, $LR0;
	s4addl $LR0, $LR1, $LR0;
	ldl $LR3, ($LR0);
	ldah $LR1, (matrixB>>16)+((matrixB>>15)&1);
	lda $LR1, matrixB & 0xFFFF($LR1);
	addl $GR0, $LR2, $LR0;
	s4addl $LR0, $LR1, $LR1;
	ldl $LR0, ($LR1);
	mull $LR0, $LR3, $LR1;
	swch;
	addl $LR1, 1, $LR1;
	addl $DR0, $LR1, $SR0;
	end;
	
end: