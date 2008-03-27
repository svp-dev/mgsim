// matmul1.asm

N = 5; // matrix size (only square matrices supported)
BLOCK1 = 5; // block size for thread1

matrixA = 0x100000;
matrixB = 0x200000;
matrixC = 0x1000;

/*
 * Multiply matrixA by matrixB and store result in matrixC. Single depth uTC version.
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
	setlimit $R0, N-1;
	setblock $R0, BLOCK1;
	setregsi $R0, 1, 0, 9;
	cred $R0, thread1;
	
	//	sync(fam1);
	addl $R0, $R31, $R31;
	swch;
	addl $R31, $R31, $R31;
	end;

.align 64;
thread1:
	.registers 1 0 9  0 0 0;	// GR,SR,LR, GF,SF,LF
	// register allocation:
	// R0: misc.
	// R1: misc
	// R2: i
	// R3: j
	// R4: jN
	// R5: v
	// R6: k
	// R7: kN
	// R8: misc
	
	//		index int j; int i,k,jN,kN,v;
	addl $LR0, $R31, $LR3;
	//		jN=j*N;
	mull $LR0, N, $LR4;
	//		for (i=0; i<N; i++)
	addl $R31, $R31, $LR2;
L5:
	cmplt $LR2, N, $LR0;
	beq $LR0, L6;
	//		{
	//			for (v=k=kN=0; k<N; k++, kN+=N)
	addl $R31, $R31, $LR5;
	addl $R31, $R31, $LR6;
	addl $R31, $R31, $LR7;
L7:
	cmplt $LR6, N, $LR0;
	beq $LR0, L8;
	//				v += matrixA[kN+i] * matrixB[jN+k];
	ldah $LR1, (matrixA>>16)+((matrixA>>15)&1);
	lda $LR1, matrixA & 0xFFFF($LR1);
	addl $LR7, $LR2, $LR0;
	s4addl $LR0, $LR1, $LR1;
	ldl $LR8, ($LR1);
	ldah $LR1, (matrixB>>16)+((matrixB>>15)&1);
	lda $LR1, matrixB & 0xFFFF($LR1);
	addl $LR4, $LR6, $LR0;
	s4addl $LR0, $LR1, $LR1;
	ldl $LR0, ($LR1);
	mull $LR0, $LR8, $LR1;
	addl $LR5, $LR1, $LR5;
	addl $LR6, 1, $LR6;
	addl $LR7, N, $LR7;
	br $R31, L7;
L8:
	//			matrixC[jN+i] = v;
	ldah $LR1, (matrixC>>16)+((matrixC>>15)&1);
	lda $LR1, matrixC & 0xFFFF($LR1);
	addl $LR4, $LR2, $LR0;
	s4addl $LR0, $LR1, $LR1;
	stl $LR5, ($LR1);
	//		}
	addl $LR2, 1, $LR2;
	br $R31, L5;
L6:
	addl $R31, $R31, $R31;
	end;

