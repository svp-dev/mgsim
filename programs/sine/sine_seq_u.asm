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
	br $R31, _sin_1E;
	_sin_1S:

		addq $R0, 1, $R0;
		mulq $R1, $R0, $R1;
		addq $R0, 1, $R0;
		mulq $R1, $R0, $R1;
		
		mult $F1, $F2, $F1;
		mult $F1, $F2, $F1;
		
		// $F3 = (float)$R1;
		itoft $R1, $F3;
		cvtqt $F3, $F3;

		// Negate $F3 on every other iteration
		cpysn $F4, $F4, $F4;
		cpysn $F3, $F3, $F5;
		fcmovlt $F4, $F5, $F3;
		
		// $F3 = $F1 / $F3;
		divt $F1, $F3, $F6;
		addt $F0, $F6, $F0;
	
	
		subq $R2, 1, $R2;
	_sin_1E:
	bne $R2, _sin_1S;
	end;
