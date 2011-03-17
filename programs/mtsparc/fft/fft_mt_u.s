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
	
	clr      %7
	allocateng %7, 0, 0, 0, 0
	setstartng %7, %11    	! start = M
    swch
	setlimitng %7, 0    		! limit = 0
	setstepng  %7, -1
	setblockng %7, 2
	cred      _FFT_1, %7
	mov      %7, %0

    .ifdef DO_BIT_REVERSAL
    swch

    clr      %8
	allocateng %8, 0, 0, 0, 0
	sub      %3, 16, %3
	setlimitng %8, %3	    	! limit = &X[N - 1]
    swch
    setblockng %8, BLOCK_POST
	setstartng %8, %1	    	! start = &X[0]
	setstepng  %8, 16	    	! step  = 16
	
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
	.registers 2 1 7  0 0 0	    ! GR,SR,LR, GF,SF,LF	
_FFT_POST:
							! %tl0 = &X[i]
	mov %td0, %tl1         	! %tl1 = j
	mov %tg0, %tl2        	! %tl2 = X

    clr      %tl3
	allocateng %tl3, 0, 0, 0, 0
	cred _FFT_POST_SWAP, %tl3
    swch

    ! k = N / 2;
    mov %tg1, %tl4    	    ! %tl4 = k
    mov %tl1, %tl5	        ! %tl5 = j
    
    ! while (k - 1 < j) {
    ba 2f
1:
    	! j = j - k;
    	sub %tl5, %tl4, %tl5
    	
    	! k = k / 2;
    	srl %tl4, 1, %tl4
    !
    ! }
2:
    sub %tl4, 1, %tl6
    cmp %tl6, %tl5
    blt 1b
    
    ! j = j + k;
    add %tl5, %tl4, %ts0
    
    ! sync
    mov %tl3, %0
	end


    .align 64
!
! %tg0 = &X[i];
! %tg1 = j
! %tg2 = X
!
	.registers 3 0 1  0 0 8
_FFT_POST_SWAP:
	sll %tg1,   4, %tl0
	add %tl0, %tg2, %tl0  	! %tl0 = &X[j]
	
	! if (i < j) {
	cmp %tg0, %tl0
	blt 1f
	end

1:
	! Swap X[i] and X[j]
    ldd [%tg0+0], %tlf0
    ldd [%tg0+8], %tlf2
    ldd [%tl0+0], %tlf4
    ldd [%tl0+8], %tlf6
    std %tlf0, [%tl0+0]
    std %tlf2, [%tl0+8]
    std %tlf4, [%tg0+0]
    std %tlf6, [%tg0+8]
	end
    .endif

    .align 64
!
! for (int k = M; k > 0; k--) {
!
! %tg0  = X
! %tg1  = &_cos_sin[-1]
! %tg2  = &X[N]
! %tg3  = 1
! %tgf0 = 1.0
! %ts0  = token
! %tl0  = k (M..1)
!
	.registers 4 1 5  1 0 8 	! GR,SR,LR, GF,SF,LF	
_FFT_1:
    ! complex S = {cos(PI/LE2), -sin(PI/LE2)} = _cos_sin[k - 1];
    sll  %tl0,   4, %tl4
    add  %tl4, %tg1, %tl4
    ldd  [%tl4+0], %tlf0
    ldd  [%tl4+8], %tlf2			! %tlf0 - %tlf3 = S

    ! complex U = {1.0, 0.0};
    fstod %tgf0, %tlf4
    fstod %f0,  %tlf6  	        ! %tlf4 - %tlf7 = U
        	
    ! int LE  = 1 << k
    ! int LE2 = LE / 2
    sll  %tg3, %tl0, %tl2		
    sll  %tl2,   4, %tl2		    ! %tl2 = LE * 16
    
    srl  %tl2,   1, %tl3
	add  %tl3, %tg0, %tl3	    	! %LR3 = &X[LE2]
    mov  %tg2, %tl1	        	! %LR1 = &X[N]
    
    clr      %tl4
	allocateng %tl4, 0, 0, 0, 0
	setlimitng %tl4, %tl3	    	! limit = &X[LE2]
	setstartng %tl4, %tg0	    	! start = &X[0]
	setstepng  %tl4, 16	    	! step  = 16
	
    mov   %tg0,  %tl0;	        ! %tl0 = X
	fcmps %tlf0, %tlf2    		! Wait for S
	swch
    
    mov  %td0, %0	        	! wait for token
	cred  _FFT_2, %tl4
	mov  %tl4, %ts0	        	! sync and write token
	end


    .align 64
!	
! for (j = 0; j < LE2; j++) {
!
! %tgf0 - %tgf3 = S
! %tsf0 - %tsf3 = U
! %tg0 = X
! %tg1 = &X[N]
! %tg2 = LE * 16
! %tg3 = &X[LE2]
! %tl0 = &X[j]
!
	.registers 4 0 2  4 4 12	! GR,SR,LR, GF,SF,LF
_FFT_2:
    clr      %tl1
	allocateng %tl1, 0, 0, 0, 0
	setstartng %tl1, %tl0		! start = &X[j];
	setlimitng %tl1, %tg1		! limit = &X[N];
	setstepng  %tl1, %tg2		! step = LE * 16;
	
	srl  %tg2, 1, %tl0		! %tl0 = LE2 * 16 (= LE * 16 / 2)
	fmovs %tdf0, %tlf0
	fmovs %tdf1, %tlf1
	fmovs %tdf2, %tlf2
	fmovs %tdf3, %tlf3 		! %tlf0 - %tlf3 = U
	
	cred _FFT_3, %tl1
	
	! U = U * S;
	fmuld %tlf0, %tgf0,  %tlf4		! %tlf4  = U.re * S.re
	fmuld %tlf2, %tgf2,  %tlf6		! %tlf6  = U.im * S.im
	fmuld %tlf2, %tgf0,  %tlf8		! %tlf8  = U.im * S.re
	fmuld %tlf0, %tgf2,  %tlf10	! %tlf10 = U.re * S.im
	fsubd %tlf4, %tlf6,  %tlf4		! U.re  = U.re * S.re - U.im * S.im;
	faddd %tlf8, %tlf10, %tlf6		! U.im  = U.im * S.re + U.re * S.im;
	fmovs %tlf4, %tsf0
	fmovs %tlf5, %tsf1
	fmovs %tlf6, %tsf2
	fmovs %tlf7, %tsf3

	! sync
	mov %tl1, %0
	end


    .align 64
!	
! for (i = j; i < N; i += LE) {
!
! %tgf0 - %tgf3 = U
! %tg0 = LE2 * 16
! %tl0 = &X[i]
!
	.registers 1 0 2  4 0 12 	! GR,SR,LR, GF,SF,LF	
_FFT_3:
	ldd [%tl0+0], %tlf0
	ldd [%tl0+8], %tlf2			! %tlf0 - %tlf3 = X[i]
	
	add %tl0, %tg0, %tl1	    	! %tl1 = &X[ip] = &X[i + LE2];
	ldd [%tl1+0], %tlf4
	ldd [%tl1+8], %tlf6			! %tlf4 - %tlf7 = X[ip]
				
	! complex T = X[i] + X[ip];
	faddd %tlf0, %tlf4, %tlf8		! %tlf8,  %tlf9  = X[i].re + X[ip].re
	faddd %tlf2, %tlf6, %tlf10		! %tlf10, %tlf11 = X[i].im + X[ip].im

	! X[i] = T;
	std %tlf8,  [%tl0+0]
	std %tlf10, [%tl0+8]

	! ! X[ip] = (X[i] - X[ip]) * U;
	! X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
	! X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re - X[ip].re) * U.im;
				
	fsubd %tlf0, %tlf4, %tlf8		! %tlf8  = X[i].re - X[ip].re
	fsubd %tlf2, %tlf6, %tlf10		! %tlf10 = X[i].im - X[ip].im
				
	fmuld %tlf8,  %tgf0, %tlf4
	fmuld %tlf10, %tgf2, %tlf6
	fsubd %tlf4,  %tlf6, %tlf4
	std   %tlf4,  [%tl1+0]

	fmuld %tlf10, %tgf0, %tlf4
	fmuld %tlf8,  %tgf2, %tlf6
	faddd %tlf4,  %tlf6, %tlf4
	std   %tlf4,  [%tl1+8]
				
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
