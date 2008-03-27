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

TAYLOR_ITERATORS = 12;

/****************************************
 * sin(x), Microthreaded version
 ****************************************/
 
/*
main:
	addq $R31, 2, $R0;
	itoft $R0, $F0;
	cvtqt $F0, $F0;

	// sin(x)
	bis $R31, 1, $R0;	// $R0 = 1 (factorial)
	cpys $F0,$F0,$F0;	// $F0 = x
	cpys $F0,$F0,$F1;	// $F1 = x (iteration)
	cpys $F0,$F0,$F2;	// $F2 = x (power series)
	cre $R2, tcb;
	addl $R2, $R31, $R31;
	end;
	
.align 64;
TCB tcb = {
	1, TAYLOR_ITERATORS, 2, _sin, 2,
	1 | (0 << 5) | (9 << 10),	// $R: G,S,L
	0 | (0 << 5) | (0 << 10),	// $F: G,S,L
	0
};

// [in]  $GF0 = x
// [in]  $DR0 = factorial[i-1]
// [in]  $DF0 = iter[i-1]
// [in]  $DF1 = pow_x[i-1]
// [out] $SR0 = factorial[i]
// [out] $SF0 = iter[i]
// [out] $SF1 = pow_x[i]
_sin:
	.registers_int 0 1 3;		// G,S,L
	.registers_flt 1 3 1;		// G,S,L
	
	bis  $LR0, 1, $LR2;
	sllq $LR2, 62, $LR2;

	mulq $DR0, $LR0, $LR1;
	addq $LR0, 1, $LR0;
	mulq $LR1, $LR0, $SR0;
	// $SR1 = factorial(j)
	
	mult $DF1, $GF0, $LF0;
	mult $LF0, $GF0, $SF1;
	// $SF0 = pow(x,j)
	
	// $LF0 = (float)$SR1;
	itoft $SR0, $LF0;
	cvtqt $LF0, $LF0;
	
	// Negate $LF0 on every other iteration
	itoft $LR2, $LF1;
	cpysn $LF0, $LF0, $LF2;
	fcmovlt $LF1, $LF2, $LF0;
	
	divt $SF1, $LF0, $LF0;
	addt $DF0, $LF0, $SF0;
	end;
*/

/****************************************
 * sin(x), Sequential version
 ****************************************/

addq $R31, 2, $R0;
itoft $R0, $F0;
cvtqt $F0, $F0;

// [in]  $F0  = x
// [in]  $R30 = ret. addr
// [out] $F0  = sin(x)
sin:
	bis $R31, 1, $R0;	// $R0 = 1
	bis $R31, 1, $R1;	// $R1 = 1 (factorial)
	cpys $F0,$F0,$F1;	// $F1 = x (power series)
	cpys $F0,$F0,$F2;	// $F2 = x
	itoft $R0, $F4;
	cvtqt $F4, $F4;		// $F4 = 1.0;
	
	lda  $R2, (TAYLOR_ITERATORS & 0xFFFF)($R31);
	ldah $R2, (TAYLOR_ITERATORS >> 16)($R2);
	br $R31, _sin_1E;
	swch;
	_sin_1S:
		
		addq $R0, 1, $R0; mulq $R1, $R0, $R1;
		addq $R0, 1, $R0; mulq $R1, $R0, $R1;
		
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



/****************************************
 * FFT Sequential version
 ****************************************/
/*
N = 256;

//
// Fast Fourier Transform.
//
// void FFT(complex X[], int M);
//
// $R0  = X
// $R1  = M
// $R30 = ret. addr.
//
	lda  $R0, (X & 0xFFFF)($R31);
	ldah $R0, (X >> 16)($R0);
	lda  $R1, (N & 0xFFFF)($R31);
	ldah $R1, (N >> 16)($R1);
	
_FFT:
	// local const int N = (1 << M);
	bis $R31, 1, $R2;
	sll $R2, $R1, $R2;			// $R2 = N
	
	// for (int k = M; k > 0; k--) {
	br $R31, _FFT_1E;			// $R1 = k
	_FFT_1S:
	
	    // local int LE = 1 << k;
	    sll $R1, 1, $R3;

	    // local int LE2 = LE / 2;
	    srl $R3, 1, $R4;
	    
	    // local complex S = {cos(PI/LE2), -sin(PI/LE2)};
	    
	    
	    // shared complex U = {1.0, 0.0};
	    
	
	
	    // for (j = 0; j < LE2; j++) {
		_FFT_2S:
	
	
		//    
	    // }
        _FFT_2E:

	//
	// }
    subq $R1, 1, $R1;
    _FFT_1E:
    bne $R1, _FFT_1S;
	end;

.align 64;
long X;
	
	*/
	
	
/****************************************
 * FFT, C version
 ****************************************/
/*
	local int k;

	for (k = M; k > 0; k--)
	{
		local  int i;
		local  int LE    = 1 << k;
		local  int LE2   = LE / 2;
		local  complex S = {cos(PI/LE2), -sin(PI/LE2)};
		shared complex U = {1.0, 0.0};
		
		for (j = 0; j < LE2; j++)
		{
			local double re;
			local int i;

			for (i = j; i < N; i += LE)
			{
				local int ip = i + LE2;
				
				// complex T =  X[i] + X[ip];
				local complex T = {
					X[i].re + X[ip].re,
					X[i].im + X[ip].im
				};

				//X[ip] = (X[i] - X[ip]) * U;
				re = X[ip].re;
				X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
				X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re -       re) * U.im;

				X[i] = T;
			}
			
			// U = U * S;
			re = U.re;
			U.re = U.re * S.re - U.im * S.im;
			U.im = U.im * S.re +   re * S.im;
		}
	}

	j = 0;
	for (i = 0; i < N - 1; i++)
	{
		if (i < j)
		{
			// Swap X[i] and X[j]
			complex T = X[j];
			X[j] = X[i];
			X[i] = T;
		}

		k = N/2;
		while (k - 1 < j)
		{
			j = j - k;
			k = k / 2;
		}
		j = j + k;
	}
}
*/