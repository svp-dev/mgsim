/****************************************
 * FFT Sequential version
 ****************************************/

// M = log2(array size);
M = 4;

// Array size
SIZE = (1 << M) * 16;

//
// Fast Fourier Transform.
//
// void FFT(complex X[], int M);
//
// $R0  = X
// $R1  = M
// $R30 = ret. addr.
//
	lda   $R0, LOWORD(X)($R31);
	ldah  $R0, HIWORD(X)($R0);			// $R0 = X
	lda   $R1, LOWORD(M)($R31);
	ldah  $R1, HIWORD(M)($R1);			// $R1 = M
	bis   $R31, 0x3, $R2;
	sll   $R2, 8, $R2;
	bis   $R2, 0xFF, $R2;
	sll   $R2, 52, $R2;
	itoft $R2, $F10;					// $F10 = 1.0
	lda   $R2, LOWORD(_cos_sin - 16)($R31);
	ldah  $R2, HIWORD(_cos_sin - 16)($R2);	// $R2 = _cos_sin
	
_FFT:
	// local const int N = (1 << M);
	bis $R31, 1, $R3;
	sll $R3, $R1, $R3;			// $R3 = N
	
	// for (int k = M; k > 0; k--) {
	_FFT_1S:					// $R1 = k
	
	    // local int LE = 1 << k;
	    addq $R31,  1, $R4;
	    sll  $R4, $R1, $R4;		// $R4 = LE

	    // local int LE2 = LE / 2;
	    srl $R4, 1, $R5;		// $R5 = LE2
	    
	    // local complex S = {cos(PI/LE2), -sin(PI/LE2)};
	    sll $R1, 4, $R6;
	    addq $R6, $R2, $R6;
	    ldt $F0, 0($R6);
	    ldt $F1, 8($R6);		// $F0, $F1 = S
	    	
	    // shared complex U = {1.0, 0.0};
	    cpys $F10, $F10, $F2;
	    cpys $F31, $F31, $F3;	// $F2, $F3 = U

	    // for (j = 0; j < LE2; j++) {
	    bis $R31, $R31, $R6;	// $R6 = j
		_FFT_2S:

			// for (i = j; i < N; i += LE) {
		    bis $R31, $R6, $R7;	// $R7 = i
			_FFT_3S:
			
				// local int ip = i + LE2;
				addq $R7, $R5, $R8; // $R8 = ip
				sll $R8, 4, $R8;
				addq $R8, $R0, $R8;	// $R8 = &X[ip]
				sll $R7, 4, $R9;
				addq $R9, $R0, $R9;	// $R9 = &X[i]
				
				// // complex T = X[i] + X[ip];
				// local complex T = {
				//	X[i].re + X[ip].re,
				//	X[i].im + X[ip].im
				// };
				
				ldt $F4, 0($R9);
				ldt $F5, 8($R9);	// $F4, $F5 = X[i]
				ldt $F6, 0($R8);
				ldt $F7, 8($R8);	// $F6, $F7 = X[ip]
				
				addt $F4, $F6, $F8;
				addt $F5, $F7, $F9;	// $F8, $F9 = T = X[i] + X[ip]

				// // X[ip] = (X[i] - X[ip]) * U;
				// re = X[ip].re;
				// X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
				// X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re -       re) * U.im;
				
				subt $F4, $F6, $F4;	// $F4 = X[i].re - X[ip].re
				subt $F5, $F7, $F5;	// $F5 = X[i].im - X[ip].im
				
				mult $F4, $F2, $F6;
				mult $F5, $F3, $F7;
				subt $F6, $F7, $F6;
				stt $F6, 0($R8);

				mult $F5, $F2, $F6;
				mult $F4, $F3, $F7;
				addt $F6, $F7, $F6;
				stt $F6, 8($R8);
				

				// X[i] = T;
				stt $F8, 0($R9);
				stt $F9, 8($R9);
			
			//
			// }
			addq $R7, $R4, $R7;
			_FFT_3E:
			subq $R3, $R7, $R8;
			bgt $R8, _FFT_3S;
			
			// $F0, $F1 = S
			// $F2, $F3 = U

			// // U = U * S;
			// re = U.re;
			// U.re = U.re * S.re - U.im * S.im;
			// U.im = U.im * S.re +   re * S.im;
			
			mult $F2, $F0, $F4;	// $F4 = U.re * S.re
			mult $F3, $F1, $F5;	// $F5 = U.im * S.im
			mult $F3, $F0, $F6;	// $F6 = U.im * S.re
			mult $F2, $F1, $F7;	// $F7 = U.re * S.im
			subt $F4, $F5, $F2;
			addt $F6, $F7, $F3;
		//    
	    // }
	    addq $R6, 1, $R6;
        _FFT_2E:
		subq $R5, $R6, $R7;
        bne $R7, _FFT_2S;

	//
	// }
    subq $R1, 1, $R1;
    _FFT_1E:
    bne $R1, _FFT_1S;
    
    // j = 0;
    bis $R31, $R31, $R1;	// $R1 = j
    
    // for (i = 0; i < N - 1; i++) {
    bis $R31, $R31, $R2;	// $R2 = i
    _FFT_4S:
    
    	// if (i < j) {
    	subq $R2, $R1, $R4;
    	bge $R4, _FFT_5;
        	// // Swap X[i] and X[j]
            
            // complex T = X[j];
            sll  $R1, 4,   $R4;
            addq $R4, $R0, $R4;
            ldt $F0, 0($R4);
            ldt $F1, 8($R4);
            
            // X[j] = X[i];
            // X[i] = T;
            sll  $R2, 4,   $R5;
            addq $R5, $R0, $R5;
            ldt $F2, 0($R5);
            ldt $F3, 8($R5);
            stt $F0, 0($R5);
            stt $F1, 8($R5);
            stt $F2, 0($R4);
            stt $F3, 8($R4);            

        //
        // }
        _FFT_5:

        // k = N/2;
        srl $R3, 1, $R4;	// $R4 = k
        
        // while (k - 1 < j) {
        br $R31, _FFT_6E;
        _FFT_6S:
        	// j = j - k;
        	subq $R1, $R4, $R1;
        	
        	// k = k / 2;
        	srl $R4, 1, $R4;
        //
        // }
        _FFT_6E:
        subq $R4,   1, $R5;
        subq $R5, $R1, $R5;
        blt $R5, _FFT_6S;
        
        // j = j + k;
        addq $R1, $R4, $R1;
	
	//
	// }
	addq $R2,   1, $R2;
	_FFT_4E:
	subq $R3, $R2, $R4;
	subq $R4,   1, $R4;
	bne $R4, _FFT_4S;

	end;
//
// END FFT
//	    

/****************************************
 * Sine/Cosine lookup table
 ****************************************/
/* This table stores pairs of
 * { cos(PI/x), -sin(PI/x) }
 * that are used in the FFT calculation where x is a power of two.
 ****************************************/
 PI = 3.1415926535897932384626433832795;
 
.align 64;
_cos_sin:
	double _cos_1  =  cos(PI/1);
	double _sin_1  = -sin(PI/1);
	double _cos_2  =  cos(PI/2);
	double _sin_2  = -sin(PI/2);
	double _cos_3  =  cos(PI/4);
	double _sin_3  = -sin(PI/4);
	double _cos_4  =  cos(PI/8);
	double _sin_4  = -sin(PI/8);
	double _cos_5  =  cos(PI/16);
	double _sin_5  = -sin(PI/16);
	double _cos_6  =  cos(PI/32);
	double _sin_6  = -sin(PI/32);
	double _cos_7  =  cos(PI/64);
	double _sin_7  = -sin(PI/64);
	double _cos_8  =  cos(PI/128);
	double _sin_8  = -sin(PI/128);
	double _cos_9  =  cos(PI/256);
	double _sin_9  = -sin(PI/256);
	double _cos_10 =  cos(PI/512);
	double _sin_10 = -sin(PI/512);
	double _cos_11 =  cos(PI/1024);
	double _sin_11 = -sin(PI/1024);
	double _cos_12 =  cos(PI/2048);
	double _sin_12 = -sin(PI/2048);
	double _cos_13 =  cos(PI/4096);
	double _sin_13 = -sin(PI/4096);
	double _cos_14 =  cos(PI/8192);
	double _sin_14 = -sin(PI/8192);
	double _cos_15 =  cos(PI/16384);
	double _sin_15 = -sin(PI/16384);
	double _cos_16 =  cos(PI/32768);
	double _sin_16 = -sin(PI/32768);

.align 0x1000;
X:
	double =  1.0; double = 0;
	double =  2.0; double = 0;
	double =  3.0; double = 0;
	double =  4.0; double = 0;
	double =  5.0; double = 0;
	double =  6.0; double = 0;
	double =  7.0; double = 0;
	double =  8.0; double = 0;
	double =  9.0; double = 0;
	double = 10.0; double = 0;
	double = 11.0; double = 0;
	double = 12.0; double = 0;
	double = 13.0; double = 0;
	double = 14.0; double = 0;
	double = 15.0; double = 0;
	double = 16.0; double = 0;
