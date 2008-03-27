/****************************************
 * sin(x), Microthreaded version
 ****************************************/

// Don't use more than 9 iterations or the factorial will overflow!
TAYLOR_ITERATIONS = 9;

main:
    // Calculate the sine of 2 (in radians)
    addq $R31, 2, $R0;
	itoft $R0, $F0;
	cvtqt $F0, $F0;

	// sin(x)
	bis $R31, 1, $R0;	 // $R0 = 1 (factorial)
	cpys $F0,$F0,$F1;	 // $F1 = x (iteration)
	mult $F0,$F0,$F0;	 // $F0 = x**2
	cpys $F1,$F1,$F2;	 // $F2 = x (power series)
	cpys $F0, $F0, $F31; // sync on mul
	allocate $R2;
	setstart $R2, 2;
	setlimit $R2, TAYLOR_ITERATIONS * 2;
	setstep  $R2, 2;
	setblock $R2, 1;
	cred $R2, _sin;
	bis $R31, $R2, $R31;
	end;
	
// [in]  $GF0 = x
// [in]  $DR0 = factorial[i-1]
// [in]  $DF0 = iter[i-1]
// [in]  $DF1 = pow_x[i-1]
// [out] $SR0 = factorial[i]
// [out] $SF0 = iter[i]
// [out] $SF1 = pow_x[i]
_sin:
	.registers 0 1 3 1 2 3;		// GR,SR,LR, GF,SF,LF
	
	mult $DF1, $GF0, $LF0; swch;
	bis $LR0, 1, $LR2;
	sll $LR2, 62, $LR2;
	mulq $DR0, $LR0, $LR1; swch;
	addq $LR0, 1, $LR0;
	mulq $LR1, $LR0, $SR0;
	cpys $LF0, $LF0, $SF1; swch;

	itoft $SR0, $LF0;
	cvtqt $LF0, $LF0;
	itoft $LR2, $LF1;
	cpysn $LF0, $LF0, $LF2;
	fcmovlt $LF1, $LF2, $LF0;
	divt $SF1, $LF0, $LF0;
	addt $DF0, $LF0, $LF0; swch;
	cpys $LF0, $LF0, $SF0;
	end;
