// matmul2.asm

N = 15; // matrix size (only square matrices supported)
BLOCK1 = 5; // block size for thread1
BLOCK2 = 5; // block size for thread2

matrixA = 0x100000;
matrixB = 0x200000;
matrixC = 0x1000;

/*
 * Multiply matrixA by matrixB and store result in matrixC. Depth 2 uTC version.
 */
main:
	// register allocation:
	// R0: misc.
	// R1: misc
	// R2: i
	// R3: N*N
	// R4: matrixA
	// R5: matrixB

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
L2:
	//	create (fam1; 0; N-1;)
	allocate $R0;
	setlimit $R0, N - 1;
	setblock $R0, BLOCK1;
	setregsi $R0, 0, 0, 2;
	cred $R0, thread1;
	//	sync(fam1);
	addl $R0, $R31, $R31;
	swch;
	addl $R31, $R31, $R31;
	end;

.align 64;
thread1:
	.registers 0 0 2  0 0 0;		// GR,SR,LR, GF,SF,LF
	// register allocation:
	// LR0: jN
	// LR1: misc
	
	//		index int j; int jN;
	//		jN=j*N;
	mull $LR0, N, $LR0;
	//		create (fam2; 0; N-1;)
	allocate $LR1;
	setlimit $LR1, N - 1;
	setblock $LR1, BLOCK2;
	setregsi $LR1, 1, 0, 7;
	cred $LR1, thread2;
	//		sync(fam2);
	addl $LR1, $R31, $R31;
	end;

.align 64;
thread2:
	.registers 1 0 7  0 0 0;		// GR,SR,LR, GF,SF,LF
	// register allocation:
	// GR0: jN
	// R0: misc.
	// R1: misc
	// R2: i
	// R3: v
	// R4: k
	// R5: kN
	// R6: misc

	//			index int i; int k,kN,v;
	addl $LR0, $R31, $LR2;
	//			for (v=k=kN=0; k<N; k++, kN+=N)
	addl $R31, $R31, $LR3;
	addl $R31, $R31, $LR4;
	addl $R31, $R31, $LR5;
L7:
	cmplt $LR4, N, $LR0;
	beq $LR0, L8;
	//				v += matrixA[kN+i] * matrixB[jN+k];
	ldah $LR1, (matrixA>>16)+((matrixA>>15)&1);
	lda $LR1, matrixA & 0xFFFF($LR1);
	addl $LR5, $LR2, $LR0;
	s4addl $LR0, $LR1, $LR1;
	ldl $LR6, ($LR1);
	ldah $LR1, (matrixB>>16)+((matrixB>>15)&1);
	lda $LR1, matrixB & 0xFFFF($LR1);
	addl $GR0, $LR4, $LR0;
	s4addl $LR0, $LR1, $LR1;
	ldl $LR0, ($LR1);
	mull $LR0, $LR6, $LR1;
	addl $LR3, $LR1, $LR3;
	addl $LR4, 1, $LR4;
	addl $LR5, N, $LR5;
	br $R31, L7;
L8:
	//			matrixC[jN+i] = v;
	ldah $LR1, (matrixC>>16)+((matrixC>>15)&1);
	lda $LR1, matrixC & 0xFFFF($LR1);
	addl $GR0, $LR2, $LR0;
	s4addl $LR0, $LR1, $LR1;
	stl $LR3, ($LR1);
	end;
