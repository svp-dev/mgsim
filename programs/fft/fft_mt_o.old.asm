/****************************************
 * FFT Microthreaded version
 ****************************************/

// M = log2(array size);
M = 16;

//BLOCK1     = M;
BLOCK2     = 4;
BLOCK_POST = 32;

_main:
	lda   $R0, LOWORD(X)($R31);
	ldah  $R0, HIWORD(X)($R0);				// $R0 = X
	lda   $R3, LOWORD(M)($R31);
	ldah  $R3, HIWORD(M)($R3);				// $R3 = M

//
// Fast Fourier Transform.
//
// void FFT(complex X[], int M);
//
// $R0  = X
// $R3  = M
//
_FFT:
	lda   $R1, LOWORD(_cos_sin - 16)($R31);
	ldah  $R1, HIWORD(_cos_sin - 16)($R1);	// $R1 = &_cos_sin[-1]
	
	bis   $R31, 1, $R4;		// $R4 = 1
	itoft $R4, $F0;
	cvtqt $F0, $F0;			// $F0 = 1.0

	// local const int N = (1 << M);
	sll   $R4, $R3, $R9;	// $R9 = N
	subq  $R9,   1, $R2;
	sll   $R2,   4, $R2;
	addq  $R2, $R0, $R2;	// $R2 = &X[N - 1]
	
	subq  $R3,   1, $R5;	// $R5 = M - 1 (doubles as token)

	allocate $R6;			// start = 0
	setlimit $R6, $R5;		// limit = M - 1
	setregsi $R6, 5, 1, 5;
	setregsf $R6, 1, 0, 4;
	//setblock $R6, BLOCK1;
	cred     $R6, _FFT_1;
	
	allocate $R7;
	subq     $R2, 16, $R8;
	setlimit $R7, $R8;		// limit = &X[N - 2]
	setstart $R7, $R0;		// start = &X[0]
	setstep  $R7, 16;		// step  = 16
	setregsi $R7, 2, 1, 7;
	setregsf $R7, 0, 0, 4;
	setblock $R7, BLOCK_POST;
	
	// sync
	bis $R31, $R6, $R31; swch;
	
							// $R0 = X
	srl $R9,     1, $R1;	// $R1 = N / 2
	bis $R31, $R31, $R2;	// $R2 = j = 0
	cred $R7, _FFT_POST;
	
	// sync
	bis $R31, $R7, $R31;
	end;
//
// END FFT
//	    

//
// for (i = 0; i < N - 1; i++) {
//
// $GR0 = X
// $GR1 = N / 2
// $SR0 = j
// $LR0 = &X[i]
//
.align 64;
_FFT_POST:
	.registers 2 1 7  0 0 0;	// GR,SR,LR, GF,SF,LF
	
	allocate $LR3;
	setregsi $LR3, 3, 0, 2;
	setregsf $LR3, 0, 0, 4;
								// $LR0 = &X[i]
	bis $DR0, $DR0, $LR1; swch;	// $LR1 = j
	bis $GR0, $GR0, $LR2;		// $LR2 = X
	cred $LR3, _FFT_POST_SWAP;

    // k = N / 2;
    bis $GR1, $GR1, $LR4;		// $LR4 = k
    bis $LR1, $LR1, $LR5;		// $LR5 = j
    
    // while (k - 1 < j) {
    br $R31, _FFT_POST_2E;
    _FFT_POST_2S:
    	// j = j - k;
    	subq $LR5, $LR4, $LR5;
    	
    	// k = k / 2;
    	srl $LR4, 1, $LR4;
    //
    // }
    _FFT_POST_2E:
    subq $LR4,    1, $LR6;
    subq $LR6, $LR5, $LR6;
    blt $LR6, _FFT_POST_2S; swch;
    
    // j = j + k;
    addq $LR5, $LR4, $SR0;
    
    // sync
    bis $R31, $LR3, $R31;
	end;

//
// $GR0 = &X[i];
// $GR1 = j
// $GR2 = X
//
.align 64;
_FFT_POST_SWAP:
	.registers 3 0 2  0 0 4;
	sll  $GR1,    4, $LR0;
	addq $LR0, $GR2, $LR0;	// $LR0 = &X[j]
	
	// if (i < j) {
	subq $GR0, $LR0, $LR1;
	blt $LR1, _FFT_POST_SWAP_1;
	end;

_FFT_POST_SWAP_1:
	// Swap X[i] and X[j]
    ldt $LF0, 0($GR0);
    ldt $LF1, 8($GR0);
    ldt $LF2, 0($LR0);
    ldt $LF3, 8($LR0);
    stt $LF0, 0($LR0); swch;
    stt $LF1, 8($LR0); swch;
    stt $LF2, 0($GR0); swch;
    stt $LF3, 8($GR0);
	end;

//
// for (int k = M; k > 0; k--) {
//
// $GR0 = X
// $GR1 = &_cos_sin[-1]
// $GR2 = &X[N - 1]
// $GR3 = M
// $GR4 = 1
// $GF0 = 1.0
// $SR0 = token
// $LR0 = k
//
.align 64;
_FFT_1:
	.registers 5 1 5  1 0 4;	// GR,SR,LR, GF,SF,LF
	
	subq $GR3, $LR0, $LR0;		// $LR0 = k ([M..1])
	
    // complex S = {cos(PI/LE2), -sin(PI/LE2)} = _cos_sin[k - 1];
    sll  $LR0,    4, $LR4;
    addq $LR4, $GR1, $LR4;
    ldt  $LF0, 0($LR4);
    ldt  $LF1, 8($LR4);			// $LF0, $LF1 = S

    // complex U = {1.0, 0.0};
    cpys $GF0, $GF0, $LF2;
    cpys $F31, $F31, $LF3;		// $LF2, $LF3 = U
        	
    // int LE  = 1 << k;
    // int LE2 = LE / 2;
    sll  $GR4, $LR0, $LR2;		
    sll  $LR2,    4, $LR2;		// $LR2 = LE * 16
    
    srl  $LR2,    1, $LR3;
	addq $LR3, $GR0, $LR3;		// $LR3 = &X[LE2]
    bis  $GR2, $GR2, $LR1;		// $LR1 = &X[N - 1]
    
	allocate $LR4;
	subq     $LR3, 16, $LR0;
	setlimit $LR4, $LR0;		// limit = &X[LE2 - 1]
	setstart $LR4, $GR0;		// start = &X[0]
	setstep  $LR4, 16;			// step  = 16
	setregsi $LR4, 4, 0, 2;
	setregsf $LR4, 2, 2, 6;
	setblock $LR4, BLOCK2;

    bis  $R31, $GR0, $LR0;		// $LR0 = X
	cpys $LF0, $LF1, $F31; swch;// Wait for S
    
    bis  $R31, $DR0, $R31; swch;// wait for token
	cred $LR4, _FFT_2;
	bis  $R31, $LR4, $SR0; 		// sync and write token
	end;

//	
// for (j = 0; j < LE2; j++) {
//
// $GF0, $GF1 = S
// $SF0, $SF1 = U
// $GR0 = X
// $GR1 = &X[N - 1]
// $GR2 = LE * 16
// $GR3 = &X[LE2]
// $LR0 = &X[j]
//
.align 64;
_FFT_2:
	.registers 4 0 2  2 2 6;	// GR,SR,LR, GF,SF,LF

	allocate $LR1;
	setstart $LR1, $LR0;		// start = &X[j];
	setlimit $LR1, $GR1;		// limit = &X[N - 1];
	setstep  $LR1, $GR2;		// step = LE * 16;
	setregsi $LR1, 1, 0, 2;
	setregsf $LR1, 2, 0, 6;
	
	srl  $GR2,    1, $LR0;		// $LR0 = LE2 * 16 (= LE * 16 / 2)
	cpys $DF0, $DF0, $LF0; swch;
	cpys $DF1, $DF1, $LF1;		// $LF0, $LF1 = U
	
	cred $LR1, _FFT_3;
	
	// U = U * S;
	mult $LF0, $GF0, $LF2;			// $LF2 = U.re * S.re
	mult $LF1, $GF1, $LF3;			// $LF3 = U.im * S.im
	mult $LF1, $GF0, $LF4;			// $LF4 = U.im * S.re
	mult $LF0, $GF1, $LF5;			// $LF5 = U.re * S.im
	subt $LF2, $LF3, $LF2; swch;	// U.re = U.re * S.re - U.im * S.im;
	addt $LF4, $LF5, $LF3; swch;	// U.im = U.im * S.re + U.re * S.im;
	cpys $LF2, $LF2, $SF0; swch;
	cpys $LF3, $LF3, $SF1; swch;

	// sync
	bis $R31, $LR1, $R31;
	end;

//	
// for (i = j; i < N; i += LE) {
//
// $GF0, $GF1 = U
// $GR0 = LE2 * 16
// $LR0 = &X[i]
//
.align 64;
_FFT_3:
	.registers 1 0 2  2 0 6;	// GR,SR,LR, GF,SF,LF
	
	ldt $LF0, 0($LR0);
	ldt $LF1, 8($LR0);			// $LF0, $LF1 = X[i]
	
	addq $LR0, $GR0, $LR1;		// $LR1 = &X[ip] = &X[i + LE2];
	ldt $LF2, 0($LR1);
	ldt $LF3, 8($LR1);			// $LF2, $LF3 = X[ip]
				
	// complex T = X[i] + X[ip];
	addt $LF0, $LF2, $LF4; swch;	// $LF4 = X[i].re + X[ip].re
	addt $LF1, $LF3, $LF5; swch;	// $LF5 = X[i].im + X[ip].im
	
	stt $LF4, 0($LR0); swch;
	stt $LF5, 8($LR0); swch;
			
	subt $LF0, $LF2, $LF4;		// $LF4 = X[i].re - X[ip].re
	subt $LF1, $LF3, $LF5;		// $LF5 = X[i].im - X[ip].im
	
	mult $LF4, $GF0, $LF2; swch;	
	mult $LF5, $GF1, $LF3; swch;
	mult $LF5, $GF0, $LF5;
	mult $LF4, $GF1, $LF4;
	
	subt $LF2, $LF3, $LF2; swch;
	addt $LF5, $LF4, $LF5; swch;
	stt  $LF2, 0($LR1); swch;
	stt  $LF5, 8($LR1);
				
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
