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
	ldah  $R0, HIWORD(X)($R0);				// $R0 = X
	lda   $R3, LOWORD(M)($R31);
	ldah  $R3, HIWORD(M)($R3);				// $R3 = M

_FFT:
	bis   $R31, 0x3, $R1;
	sll   $R1,    8, $R1;
	bis   $R1, 0xFF, $R1;
	sll   $R1,   52, $R1;
	itoft $R1, $F0;							// $F0 = 1.0

	lda   $R1, LOWORD(_cos_sin - 16)($R31);
	ldah  $R1, HIWORD(_cos_sin - 16)($R1);	// $R1 = _cos_sin
	
	// local const int N = (1 << M);
	bis $R31,  1, $R2;
	sll $R2, $R3, $R2;						// $R2 = N
	
	bis $R31, $R31, $R4;
	
	// create
	allocate $R5;
	subq     $R3,   1, $R3;
	setlimit $R5, $R3;
	setregsi $R5, 4, 1, 5;
	setregsf $R5, 1, 0, 4;
	cred     $R5, _FFT_1;
	
	// sync
	bis $R31, $R5, $R31;
/*	
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
*/
	end;
//
// END FFT
//	    

//
// for (int k = M; k > 0; k--) {
//
// $GR0 = X
// $GR1 = _cos_sin
// $GR2 = N
// $GR3 = M
// $GF0 = 1.0
// $SR0 = token
// $LR0 = k
//
_FFT_1:
	.registers 4 1 5  1 0 4;	// GR,SR,LR, GF,SF,LF
	
    // complex S = {cos(PI/LE2), -sin(PI/LE2)} = _cos_sin[k];
    sll  $LR0,    4, $LR4;
    addq $LR4, $GR1, $LR4;
    ldt  $LF0, 0($LR4);
    ldt  $LF1, 8($LR4);			// $LF0, $LF1 = S
	    	
    // complex U = {1.0, 0.0};
    cpys $F10, $GF0, $LF2;
    cpys $F31, $F31, $LF3;		// $LF2, $LF3 = U
    
    // int LE  = 1 << k;
    // int LE2 = LE / 2;
    addq $R31,    1, $LR2;
    sll  $LR2, $LR0, $LR2;		// $LR2 = LE
    srl  $LR2,    1, $LR3;		// $LR3 = LE2
    bis  $R31, $GR0, $LR0;		// $LR0 = X
    bis  $R31, $GR2, $LR1;		// $LR1 = N
    
    // wait for token
    bis $R31, $DR0, $R31;

	// create
	allocate $LR4;
	setlimit $LR4, $LR3;
	setregsi $LR4, 4, 0, 3;
	setregsf $LR4, 2, 2, 6;
	cred     $LR4, _FFT_2;
	
	// sync and write token
	bis $R31, $LR4, $SR0;
	end;

//	
// for (j = 0; j < LE2; j++) {
//
// $GR0 = X
// $GR1 = N
// $GR2 = LE
// $GR3 = LE2
// $GF0, $GF1 = S
// $SF0, $SF1 = U
// $LR0 = j
//
_FFT_2:
	.registers 4 0 3  2 2 6;	// GR,SR,LR, GF,SF,LF

	// create
	allocate $LR2;
	setstart $LR2, $LR0;
	subq     $GR1, 1, $LR0;
	setlimit $LR2, $LR0;
	setstep  $LR2, $GR2;
	setregsi $LR2, 2, 0, 2;
	setregsf $LR2, 2, 0, 6;
	
	bis  $R31, $GR0, $LR0;		// $LR0 = X
	bis  $R31, $GR3, $LR1;		// $LR1 = LE2
	cpys $DF0, $DF0, $LF0;
	cpys $DF1, $DF1, $LF1;		// $LF0, $LF1 = U
	
	cred     $LR2, _FFT_3;
	
	// U = U * S;
	mult $LF0, $GF0, $LF2;	// $LF2 = U.re * S.re
	mult $LF1, $GF1, $LF3;	// $LF3 = U.im * S.im
	mult $LF1, $GF0, $LF4;	// $LF4 = U.im * S.re
	mult $LF0, $GF1, $LF5;	// $LF5 = U.re * S.im
	subt $LF2, $LF3, $SF0;  // U.re = U.re * S.re - U.im * S.im;
	addt $LF4, $LF5, $SF1;  // U.im = U.im * S.re + U.re * S.im;

	// sync
	bis $R31, $LR2, $R31;
	end;

//	
// for (i = j; i < N; i += LE) {
//
// $GR0 = X
// $GR1 = LE2
// $GF0, $GF1 = U
// $LR0 = i
//
_FFT_3:
	.registers 2 0 2  2 0 6;	// GR,SR,LR, GF,SF,LF
	
	// int ip = i + LE2;
	addq $LR0, $GR1, $LR1;
	sll  $LR1,    4, $LR1;
	addq $LR1, $GR0, $LR1;		// $LR1 = &X[ip]
	sll  $LR0,    4, $LR0;
	addq $LR0, $GR0, $LR0;		// $LR0 = &X[i]
				
	ldt $LF0, 0($LR0);
	ldt $LF1, 8($LR0);			// $LF0, $LF1 = X[i]
	ldt $LF2, 0($LR1);
	ldt $LF3, 8($LR1);			// $LF2, $LF3 = X[ip]
				
	// complex T = X[i] + X[ip];
	addt $LF0, $LF2, $LF4;		// $LF4 = X[i].re + X[ip].re
	addt $LF1, $LF3, $LF5;		// $LF5 = X[i].im + X[ip].im

	// X[i] = T;
	stt $LF4, 0($R9);
	stt $LF5, 8($R9);

	// // X[ip] = (X[i] - X[ip]) * U;
	// X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
	// X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re - X[ip].re) * U.im;
				
	subt $LF0, $LF2, $LF4;		// $LF4 = X[i].re - X[ip].re
	subt $LF1, $LF3, $LF5;		// $LF5 = X[i].im - X[ip].im
				
	mult $LF4, $GF0, $LF2;
	mult $LF5, $GF1, $LF3;
	subt $LF2, $LF3, $LF2;
	stt  $LF2, 0($LR1);

	mult $LF5, $GF0, $LF2;
	mult $LF4, $GF1, $LF3;
	addt $LF2, $LF3, $LF2;
	stt  $LF2, 8($LR1);
				
	end;
	    
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
