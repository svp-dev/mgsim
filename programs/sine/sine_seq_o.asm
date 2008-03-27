/****************************************
 * sin(x), Sequential version
 ****************************************/

// Don't use more than 9 iterations or the factorial will overflow!
TAYLOR_ITERATIONS = 9;

main:
    // Calculate the sine of 2 (in radians)
    addq $R31, 2, $R0;
	itoft $R0, $F0;
	cvtqt $F0, $F0;

// [in]  $F0  = x
// [out] $F0  = sin(x)
sin:
	// Initialize variables
	bis $R31, 1, $R0;	// $R0 = 1
	bis $R31, 1, $R1;	// $R1 = 1 (factorial)
	cpys $F0,$F0,$F1;	// $F1 = x (power series)
	cpys $F0,$F0,$F2;	// $F2 = x
	itoft $R0, $F4;
	cvtqt $F4, $F4;		// $F4 = 1.0;
	
	lda  $R2, LOWORD(TAYLOR_ITERATIONS)($R31);
	ldah $R2, HIWORD(TAYLOR_ITERATIONS)($R2);
	
	// We assume $R2 is not zero, so we don't jump to the test
	br $R31, _sin_1;

// This loop fits exactly in one cache line, so align it to one
.align 64;	
	_sin_1:

		mult $F1, $F2, $F1;
		addq $R0, 1, $R0;
		mulq $R1, $R0, $R1;
		addq $R0, 1, $R0;
		mult $F1, $F2, $F1;
		mulq $R1, $R0, $R1;
		itoft $R1, $F3;
		cvtqt $F3, $F3;
		
		cpysn $F4, $F4, $F4;
		cpysn $F3, $F3, $F5;
		fcmovlt $F4, $F5, $F3;
		divt $F1, $F3, $F6;
		subq $R2, 1, $R2;
		addt $F0, $F6, $F0;
	bne $R2, _sin_1;
	end;
