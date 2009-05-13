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
	allocate %8, 0, 0, 0, 0
	add      %1, 16, %9
	setstart %8, %9		    ! start = &X[1]
	sll      %6,  4, %9
	add	     %9, %1, %9
	setlimit %8, %9 		! limit = &X[N - 1]
	setstep  %8, 16 		! step  = 16
	setblock %8, BLOCK_POST
	
	mov     %2, %3	        ! %3 = j = N / 2
	cred    _FFT_POST, %8
	mov     %8, %0
	swch
	.endif

	allocate %8, 0, 0, 0, 0

	sll     %2,     4, %2   ! %2 = (N / 2) * 16;
	set      _cos_sin, %3   ! %3 = _cos_sin
    sll     %4, MAX_M, %5   ! %5 = MAX_N

	! create and sync
	setstart %8, %4		    ! start = 1
	add      %11, 1, %11
	setlimit %8, %11	    ! limit = M + 1
	setplace %8, 2          ! place = LOCAL
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
! %g0 = X
! %g1 = N / 2
! %s0 = j
! %l0 = &X[i]
!
    .align 64
_FFT_POST:
	.registers 2 1 7  0 0 0	    ! GR,SR,LR, GF,SF,LF
	
	allocate %l3, 0, 0, 0, 0
							    ! %l0 = &X[i]
	mov %d0, %l1; swch	    ! %l1 = j
	mov %g0, %l2		        ! %l2 = X
	cred _FFT_POST_SWAP, %l3
	
    ! k = N / 2;
    mov %g1, %l4		        ! %l4 = k
    mov %l1, %l5		        ! %l5 = j
    
    ! while (k <= j) {
    jmp 2f
1:
    	! j = j - k;
    	sub %l5, %l4, %l5
    	
    	! k = k / 2;
    	srl %l4, 1, %l4
    !
    ! }
2:
    cmp %l4, %l5
    ble 1b; swch
    
    ! j = j + k;
    add %l5, %l4, %s0
    
    ! sync
    mov %l3, %0
	end


    !
    ! %g0 = &X[i];
    ! %g1 = j
    ! %g2 = X
    !
    .align 64
_FFT_POST_SWAP:
	.registers 3 0 2  0 0 8
	sll  %g1,  4, %l0
	add %l0, %g2, %l0	! %l0 = &X[j]
	
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
    std %lf0, [%l0+0]; swch
    std %lf2, [%l0+8]; swch
    std %lf4, [%g0+0]; swch
    std %lf6, [%g0+8]
	end
    .endif

/*
 * for (int k = 1; k <= M; k++) {
 *
 * %g0 = X
 * %g1 = (N / 2) * 16
 * %g2 = _cos_sin
 * %g3 = 1
 * %g4 = MAX_N
 * %s0 = token
 * %l0 = k
 */
    .align 64
_FFT_1:
	.registers 5 1 6  0 0 0	    ! GR,SR,LR, GF,SF,LF
	
	print %g1, 0
	allocate %l5, 0, 0, 0, 0	! start = 0
	setlimit %l5, %g1		    ! limit = (N / 2) * 16
	setstep  %l5, 16			! step  = 16
	! setblock %l5, %g5
	
	srl  %g4, %l0, %l4		! %l4 = Z = (MAX_N >> k);
    mov  %g0, %l3		    ! %l3 = X
    mov  %g2, %l2		    ! %l2 = _cos_sin
	sub  %l0,   1, %l1		! %l1 = k - 1;
	sll  %g3, %l0, %l0
	sll  %l0,   3, %l0
	sub  %l0,   1, %l0		! %l0 = LE2 * 16 - 1
	
    mov  %d0, %0; swch      ! wait for token
	cred _FFT_2, %l5
	mov  %l5, %s0 		    ! sync and write token
	end;

/*
 * for (i = 0; i < N / 2; i++) {
 *
 * %g0 = LE2 * 16 - 1
 * %g1 = k - 1
 * %g2 = _cos_sin
 * %g3 = X
 * %g4 = Z
 * %l0 = i * 16
 */
    .align 64
_FFT_2:
	.registers 5 0 3  0 0 16    ! GR,SR,LR, GF,SF,LF
	
	and  %l0, %g0, %l1	    ! %l1 = w
	sub  %l0, %l1, %l0
	sll  %l0,   1, %l0
	add  %l0, %l1, %l0	    ! %l0 = j
	
	add %l0, %g0, %l2       ! %l2 = ip;
	add %l2, %g3, %l2	    ! %l2 = &X[ip] - 1;
	ldd [%l2+1], %lf4
	ldd [%l2+9], %lf6		! %LF4, %LF6 = X[ip]
	
	umul %l1, %g4, %l1
	add  %l1, %g2, %l1	    ! %l1 = &_cos_sin[w * Z];
	ldd  [%l1+0], %lf8
	ldd  [%l1+8], %lf10		! %LF8, %LF10 = U
				
	add %l0, %g3, %l0       ! %l0 = &X[j];

	ldd [%l0+0], %lf0
	ldd [%l0+8], %lf2		! %LF0, %LF2 = X[j]

	! complex T = U * X[ip];
	fmuld %lf8,  %lf4,  %lf12; swch
	fmuld %lf10, %lf6,  %lf14; swch
	fmuld %lf10, %lf4,  %lf4
	fmuld %lf8,  %lf6,  %lf6
	
	fsubd %lf12, %lf14, %lf12; swch
	faddd %lf4,  %lf6,  %lf14; swch	! %LF12, %LF14 = T
	
	! X[ip] = X[j] - T
	! X[j]  = X[j] + T
	fsubd %lf0, %lf12, %lf4; swch
	faddd %lf0, %lf12, %lf0
	fsubd %lf2, %lf14, %lf6; swch
	faddd %lf2, %lf14, %lf2
	
	std %lf4, [%l2+1]; swch
	std %lf6, [%l2+9]; swch
	std %lf0, [%l0+0]; swch
	std %lf2, [%l0+8]
	
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
