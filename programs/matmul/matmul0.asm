N = 10; // matrix size (only square matrices supported)

matrixA = 0x100000;
matrixB = 0x200000;
matrixC = 0x1000;

/*
 * Multiply matrixA by matrixB and store result in matrixC. Linear version.
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
	ldah $R3, HIWORD(N*N);
	lda  $R3, LOWORD(N*N)($R3);
	ldah $R4, HIWORD(matrixA) + ((matrixA >> 15) & 1);
	lda  $R4, LOWORD(matrixA)($R4);
	ldah $R5, HIWORD(matrixB) + ((matrixB >> 15) & 1);
	lda  $R5, LOWORD(matrixB)($R5);
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
	
	//	for (j=jN=0; j<N; j++, jN+=N)
	addl $R31, $R31, $R3;
	addl $R31, $R31, $R4;
L3:
	cmplt $R3, N, $R0;
	beq $R0, L4;
	//	{
	//		for (i=0; i<N; i++)
	addl $R31, $R31, $R2;
L5:
	cmplt $R2, N, $R0;
	beq $R0, L6;
	//		{
	//			for (v=k=kN=0; k<N; k++, kN+=N)
	addl $R31, $R31, $R5;
	addl $R31, $R31, $R6;
	addl $R31, $R31, $R7;
L7:
	cmplt $R6, N, $R0;
	beq $R0, L8;
	//				v += matrixA[kN+i] * matrixB[jN+k];
	ldah $R1, (matrixA>>16)+((matrixA>>15)&1);
	lda $R1, matrixA & 0xFFFF($R1);
	addl $R7, $R2, $R0;
	s4addl $R0, $R1, $R1;
	ldl $R8, ($R1);
	ldah $R1, (matrixB>>16)+((matrixB>>15)&1);
	lda $R1, matrixB & 0xFFFF($R1);
	addl $R4, $R6, $R0;
	s4addl $R0, $R1, $R1;
	ldl $R0, ($R1);
	mull $R0, $R8, $R1;
	addl $R5, $R1, $R5;
	addl $R6, 1, $R6;
	addl $R7, N, $R7;
	br $R31, L7;
L8:
	//			matrixC[jN+i] = v;
	ldah $R1, HIWORD(matrixC) + ((matrixC >> 15) & 1);
	lda $R1, LOWORD(matrixC)($R1);
	addl $R4, $R2, $R0;
	s4addl $R0, $R1, $R1;
	stl $R5, ($R1);
	//		}
	addl $R2, 1, $R2;
	br $R31, L5;
L6:
	//	}
	addl $R3, 1, $R3;
	addl $R4, N, $R4;
	br $R31, L3;
L4:
	//	return 0
	addl $R0, $R31, $R31;
	end;

