/****************************************
 * FFT Microthreaded version
 ****************************************/

    ! Comment this out if you don't want the bit reversal.
    .equ DO_BIT_REVERSAL, 1

    .file "fft_mt_o.s"
    .text

    .globl main
!
! Input:
!
! %27 = address of first instruction in main
! %11 = M
!
main:
    set X, %1             ! %1 = X

/*
 * Fast Fourier Transform.
 *
 * void FFT(complex X[], int M);
 *
 * %1  = X
 * %11 = M = log2(array size);
 */
.equ MAX_M,      16
.equ BLOCK_POST, 16

_FFT:
	mov     1, %4	        ! %4  = 1
	sll     %4, %11, %10    ! %10 = N
	sub     %10,  1, %6	    ! %6  = N - 1	(doubles as token)
	srl     %10,  1, %2	    ! %2  = N / 2

    
    .ifdef DO_BIT_REVERSAL	
    clr      %8
	allocateng %8, 0, 0, 0, 0
	add      %1, 16, %9
	setstartng %8, %9		    ! start = &X[1]
	sll      %6,  4, %9
	add	     %9, %1, %9
	setlimitng %8, %9 		! limit = &X[N - 1]
	setstepng  %8, 16 		! step  = 16
	setblockng %8, BLOCK_POST
	
	mov     %2, %3	        ! %3 = j = N / 2
	cred    _FFT_POST, %8
	mov     %8, %0
	swch
	.endif

    mov      2, %8          ! place = LOCAL
	allocateng %8, 0, 0, 0, 0

	sll     %2,     4, %2   ! %2 = (N / 2) * 16;
	set      _cos_sin, %3   ! %3 = _cos_sin
    sll     %4, MAX_M, %5   ! %5 = MAX_N

	! create and sync
	setstartng %8, %4		    ! start = 1
	add      %11, 1, %11
	setlimitng %8, %11	    ! limit = M + 1
	cred     _FFT_1, %8
	mov      %8, %1
	end
!
! END FFT
!

    .ifdef DO_BIT_REVERSAL
!
! for (i = 0; i < N - 1; i++) {
!
! %tg0 = X
! %tg1 = N / 2
! %ts0 = j
! %tl0 = &X[i]
!
    .align 64
	.registers 2 1 7  0 0 0	    ! GR,SR,LR, GF,SF,LF	
_FFT_POST:
	clr      %tl3
	allocateng %tl3, 0, 0, 0, 0
							    ! %tl0 = &X[i]
	mov %td0, %tl1; swch	    ! %tl1 = j
	mov %tg0, %tl2		        ! %tl2 = X
	cred _FFT_POST_SWAP, %tl3
	
    ! k = N / 2;
    mov %tg1, %tl4		        ! %tl4 = k
    mov %tl1, %tl5		        ! %tl5 = j
    
    ! while (k <= j) {
    ba 2f
1:
    	! j = j - k;
    	sub %tl5, %tl4, %tl5
    	
    	! k = k / 2;
    	srl %tl4, 1, %tl4
    !
    ! }
2:
    cmp %tl4, %tl5
    ble 1b; swch
    
    ! j = j + k;
    add %tl5, %tl4, %ts0
    
    ! sync
    mov %tl3, %0
	end


    !
    ! %tg0 = &X[i];
    ! %tg1 = j
    ! %tg2 = X
    !
    .align 64
	.registers 3 0 2  0 0 8
_FFT_POST_SWAP:
	sll  %tg1,  4, %tl0
	add %tl0, %tg2, %tl0	! %tl0 = &X[j]
	
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
    std %tlf0, [%tl0+0]; swch
    std %tlf2, [%tl0+8]; swch
    std %tlf4, [%tg0+0]; swch
    std %tlf6, [%tg0+8]
	end
    .endif

/*
 * for (int k = 1; k <= M; k++) {
 *
 * %tg0 = X
 * %tg1 = (N / 2) * 16
 * %tg2 = _cos_sin
 * %tg3 = 1
 * %tg4 = MAX_N
 * %ts0 = token
 * %tl0 = k
 */
    .align 64
	.registers 5 1 5  0 0 0	    ! GR,SR,LR, GF,SF,LF	
_FFT_1:
	clr      %tl4
	allocateng %tl4, 0, 0, 0, 0	! start = 0
	setlimitng %tl4, %tg1		    ! limit = (N / 2) * 16
	setstepng  %tl4, 16			! step  = 16
	! setblockng %tl4, %tg4
	
	srl  %tg4, %tl0, %tl3		! %tl3 = Z = (MAX_N >> k);
    mov  %tg0, %tl2		    ! %tl2 = X
    mov  %tg2, %tl1		    ! %tl1 = _cos_sin
	sll  %tg3, %tl0, %tl0
	sll  %tl0,   3, %tl0
	sub  %tl0,   1, %tl0		! %tl0 = LE2 * 16 - 1
	
    mov  %td0, %0; swch      ! wait for token
	cred _FFT_2, %tl4
	mov  %tl4, %ts0 		    ! sync and write token
	end;

/*
 * for (i = 0; i < N / 2; i++) {
 *
 * %tg0 = LE2 * 16 - 1
 * %tg1 = _cos_sin
 * %tg2 = X
 * %tg3 = Z
 * %tl0 = i * 16
 */
    .align 64
	.registers 4 0 3  0 0 16    ! GR,SR,LR, GF,SF,LF
_FFT_2:	
	and  %tl0, %tg0, %tl1	    ! %tl1 = w
	sub  %tl0, %tl1, %tl0
	sll  %tl0,   1, %tl0
	add  %tl0, %tl1, %tl0	    ! %tl0 = j
	
	add %tl0, %tg0, %tl2       ! %tl2 = ip;
	add %tl2, %tg2, %tl2	    ! %tl2 = &X[ip] - 1;
	ldd [%tl2+1], %tlf4
	ldd [%tl2+9], %tlf6		! %LF4, %LF6 = X[ip]
	
	umul %tl1, %tg3, %tl1
	add  %tl1, %tg1, %tl1	    ! %tl1 = &_cos_sin[w * Z];
	ldd  [%tl1+0], %tlf8
	ldd  [%tl1+8], %tlf10		! %LF8, %LF10 = U
				
	add %tl0, %tg2, %tl0       ! %tl0 = &X[j];

	ldd [%tl0+0], %tlf0
	ldd [%tl0+8], %tlf2		! %LF0, %LF2 = X[j]

	! complex T = U * X[ip];
	fmuld %tlf8,  %tlf4,  %tlf12; swch
	fmuld %tlf10, %tlf6,  %tlf14; swch
	fmuld %tlf10, %tlf4,  %tlf4
	fmuld %tlf8,  %tlf6,  %tlf6
	
	fsubd %tlf12, %tlf14, %tlf12; swch
	faddd %tlf4,  %tlf6,  %tlf14; swch	! %LF12, %LF14 = T
	
	! X[ip] = X[j] - T
	! X[j]  = X[j] + T
	fsubd %tlf0, %tlf12, %tlf4; swch
	faddd %tlf0, %tlf12, %tlf0
	fsubd %tlf2, %tlf14, %tlf6; swch
	faddd %tlf2, %tlf14, %tlf2
	
	std %tlf4, [%tl2+1]; swch
	std %tlf6, [%tl2+9]; swch
	std %tlf0, [%tl0+0]; swch
	std %tlf2, [%tl0+8]
	
	end

/****************************************
 * Sine/Cosine lookup table
 ****************************************/
/* This table stores pairs of
 * {
 *    cos(2 * i * PI / MAX_N),
 *   -sin(2 * i * PI / MAX_N)
 * }
 * that are used in the FFT calculation
 ****************************************/
    .data
    .align 64
_cos_sin:
    .include "fft_lookup_o.s"

/*
 * The input and output array
 */
    .align 64
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
    .double 17; .double 0
    .double 18; .double 0
    .double 19; .double 0
    .double 20; .double 0
    .double 21; .double 0
    .double 22; .double 0
    .double 23; .double 0
    .double 24; .double 0
    .double 25; .double 0
    .double 26; .double 0
    .double 27; .double 0
    .double 28; .double 0
    .double 29; .double 0
    .double 30; .double 0
    .double 31; .double 0
    .double 32; .double 0
    .skip X + (1 << MAX_M) * 16 - .
