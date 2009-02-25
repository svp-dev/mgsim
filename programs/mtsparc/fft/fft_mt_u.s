/****************************************
 * FFT Microthreaded version
 ****************************************/

    ! Comment this out if you don't want the bit reversal.
    .equ DO_BIT_REVERSAL, 1

    .file "fft_mt_u.s"
    .text

    .globl main
    .align 64
!
! Input:
!
! %11 = M
!
main:
    set X, %1               ! %0 = X

/*
 * Fast Fourier Transform.
 *
 * void FFT(complex X[], int M);
 *
 * %1  = X
 * %11 = M = log2(array size)
 */
.equ MAX_M,      16
.equ BLOCK_POST, 32

_FFT:
    set _cos_sin - 16, %2   ! %2 = &_cos_sin[-1]
	
	set     _one, %4
	ld      [%4], %f1       ! %f1 = 1.0
	mov     1,   %4		    ! %4 = 1

	! local const int N = (1 << M);
	sll   %4, %11, %10	    ! %10 = N
	sub   %10,  0, %5       ! %5 = tmp, doubles as token
	sll   %5,   4, %3
	add   %3,  %1, %3    	! %3 = &X[N]
	
	allocate %7, 0, 0, 0, 0
	setstart %7, %11    	! start = M
    swch
	setlimit %7, 0    		! limit = 0
	setstep  %7, -1
	setblock %7, 2
	cred      _FFT_1, %7
	mov      %7, %0

    .ifdef DO_BIT_REVERSAL
    swch

	allocate %8, 0, 0, 0, 0
	sub      %3, 16, %3
	setlimit %8, %3	    	! limit = &X[N - 1]
    swch
    setblock %8, BLOCK_POST
	setstart %8, %1	    	! start = &X[0]
	setstep  %8, 16	    	! step  = 16
	
							! %1 = X
	srl     %10,  1, %2	    ! %2 = N / 2
	clr     %3  	        ! %3 = j = 0
	cred     _FFT_POST, %8
	mov     %8, %0
    .endif

	end
!
! END FFT
!	    


    .ifdef DO_BIT_REVERSAL
    .align 64
!
! for (i = 0; i < N - 1; i++) {
!
! %GR0 = X
! %GR1 = N / 2
! %SR0 = j
! %LR0 = &X[i]
!
_FFT_POST:
	.registers 2 1 7  0 0 0	    ! GR,SR,LR, GF,SF,LF
	
							! %l0 = &X[i]
	mov %d0, %l1         	! %l1 = j
	mov %g0, %l2        	! %l2 = X

	allocate %l3, 0, 0, 0, 0
	cred _FFT_POST_SWAP, %l3
    swch

    ! k = N / 2;
    mov %g1, %l4    	    ! %l4 = k
    mov %l1, %l5	        ! %l5 = j
    
    ! while (k - 1 < j) {
    jmp 2f
1:
    	! j = j - k;
    	sub %l5, %l4, %l5
    	
    	! k = k / 2;
    	srl %l4, 1, %l4
    !
    ! }
2:
    sub %l4, 1, %l6
    cmp %l6, %l5
    blt 1b
    
    ! j = j + k;
    add %l5, %l4, %s0
    
    ! sync
    mov %l3, %0
	end


    .align 64
!
! %g0 = &X[i];
! %g1 = j
! %g2 = X
!
_FFT_POST_SWAP:
	.registers 3 0 1  0 0 8
	sll %g1,   4, %l0
	add %l0, %g2, %l0  	! %l0 = &X[j]
	
	! if (i < j) {
	cmp %g0, %l0
	blt 1f
	end

1:
	! Swap X[i] and X[j]
    ldd [%g0+0], %lf0
    ldd [%g0+8], %lf2
    ldd [%l0+0], %lf4
    ldd [%l0+8], %lf6
    std %lf0, [%l0+0]
    std %lf2, [%l0+8]
    std %lf4, [%g0+0]
    std %lf6, [%g0+8]
	end
    .endif

    .align 64
!
! for (int k = M; k > 0; k--) {
!
! %g0  = X
! %g1  = &_cos_sin[-1]
! %g2  = &X[N]
! %g3  = 1
! %gf0 = 1.0
! %s0  = token
! %l0  = k (M..1)
!
_FFT_1:
	.registers 4 1 5  1 0 8 	! GR,SR,LR, GF,SF,LF
	
    ! complex S = {cos(PI/LE2), -sin(PI/LE2)} = _cos_sin[k - 1];
    sll  %l0,   4, %l4
    add  %l4, %g1, %l4
    ldd  [%l4+0], %lf0
    ldd  [%l4+8], %lf2			! %lf0 - %lf3 = S

    ! complex U = {1.0, 0.0};
    fstod %gf0, %lf4
    fstod %f0,  %lf6  	        ! %lf4 - %lf7 = U
        	
    ! int LE  = 1 << k
    ! int LE2 = LE / 2
    sll  %g3, %l0, %l2		
    sll  %l2,   4, %l2		    ! %l2 = LE * 16
    
    srl  %l2,   1, %l3
	add  %l3, %g0, %l3	    	! %LR3 = &X[LE2]
    mov  %g2, %l1	        	! %LR1 = &X[N]
    
	allocate %l4, 0, 0, 0, 0
	setlimit %l4, %l3	    	! limit = &X[LE2]
	setstart %l4, %g0	    	! start = &X[0]
	setstep  %l4, 16	    	! step  = 16
	
    mov   %g0,  %l0;	        ! %l0 = X
	fcmps %lf0, %lf2    		! Wait for S
	swch
    
    mov  %d0, %0	        	! wait for token
	cred  _FFT_2, %l4
	mov  %l4, %s0	        	! sync and write token
	end


    .align 64
!	
! for (j = 0; j < LE2; j++) {
!
! %gf0 - %gf3 = S
! %sf0 - %sf3 = U
! %g0 = X
! %g1 = &X[N]
! %g2 = LE * 16
! %g3 = &X[LE2]
! %l0 = &X[j]
!
_FFT_2:
	.registers 4 0 2  4 4 12	! GR,SR,LR, GF,SF,LF

	allocate %l1, 0, 0, 0, 0
	setstart %l1, %l0		! start = &X[j];
	setlimit %l1, %g1		! limit = &X[N];
	setstep  %l1, %g2		! step = LE * 16;
	
	srl  %g2, 1, %l0		! %l0 = LE2 * 16 (= LE * 16 / 2)
	fmovs %df0, %lf0
	fmovs %df1, %lf1
	fmovs %df2, %lf2
	fmovs %df3, %lf3 		! %lf0 - %lf3 = U
	
	cred _FFT_3, %l1
	
	! U = U * S;
	fmuld %lf0, %gf0,  %lf4		! %lf4  = U.re * S.re
	fmuld %lf2, %gf2,  %lf6		! %lf6  = U.im * S.im
	fmuld %lf2, %gf0,  %lf8		! %lf8  = U.im * S.re
	fmuld %lf0, %gf2,  %lf10	! %lf10 = U.re * S.im
	fsubd %lf4, %lf6,  %lf4		! U.re  = U.re * S.re - U.im * S.im;
	faddd %lf8, %lf10, %lf6		! U.im  = U.im * S.re + U.re * S.im;
	fmovs %lf4, %sf0
	fmovs %lf5, %sf1
	fmovs %lf6, %sf2
	fmovs %lf7, %sf3

	! sync
	mov %l1, %0
	end


    .align 64
!	
! for (i = j; i < N; i += LE) {
!
! %gf0 - %gf3 = U
! %g0 = LE2 * 16
! %l0 = &X[i]
!
_FFT_3:
	.registers 1 0 2  4 0 12 	! GR,SR,LR, GF,SF,LF
	
	ldd [%l0+0], %lf0
	ldd [%l0+8], %lf2			! %lf0 - %lf3 = X[i]
	
	add %l0, %g0, %l1	    	! %l1 = &X[ip] = &X[i + LE2];
	ldd [%l1+0], %lf4
	ldd [%l1+8], %lf6			! %lf4 - %lf7 = X[ip]
				
	! complex T = X[i] + X[ip];
	faddd %lf0, %lf4, %lf8		! %lf8,  %lf9  = X[i].re + X[ip].re
	faddd %lf2, %lf6, %lf10		! %lf10, %lf11 = X[i].im + X[ip].im

	! X[i] = T;
	std %lf8,  [%l0+0]
	std %lf10, [%l0+8]

	! ! X[ip] = (X[i] - X[ip]) * U;
	! X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
	! X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re - X[ip].re) * U.im;
				
	fsubd %lf0, %lf4, %lf8		! %lf8  = X[i].re - X[ip].re
	fsubd %lf2, %lf6, %lf10		! %lf10 = X[i].im - X[ip].im
				
	fmuld %lf8,  %gf0, %lf4
	fmuld %lf10, %gf2, %lf6
	fsubd %lf4,  %lf6, %lf4
	std   %lf4,  [%l1+0]

	fmuld %lf10, %gf0, %lf4
	fmuld %lf8,  %gf2, %lf6
	faddd %lf4,  %lf6, %lf4
	std   %lf4,  [%l1+8]
				
	end
	    
/****************************************
 * Sine/Cosine lookup table
 ****************************************/
/* This table stores pairs of
 * {
 *    cos(PI / (1 << i)),
 *   -sin(PI / (1 << i))
 * }
 * that are used in the FFT calculation
 ****************************************/
    .data
    .align 64
_one:
    .float 1.0
    
    .align 64
    .globl _cos_sin
_cos_sin:
    .include "fft_lookup_u.s"

/*
 * The input and output array
 */
    .align 64
    .globl X
X:
    .double 1; .double 0
    .double 2; .double 0
    .double 3; .double 0
    .double 4; .double 0
    .double 5; .double 0
    .double 6; .double 0
    .double 7; .double 0
    .double 8; .double 0
    .double 9; .double 0
    .double 10; .double 0
    .double 11; .double 0
    .double 12; .double 0
    .double 13; .double 0
    .double 14; .double 0
    .double 15; .double 0
    .double 16; .double 0
    .skip ((1 << MAX_M) - 16) * 16
