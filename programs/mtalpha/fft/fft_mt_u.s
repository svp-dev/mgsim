/****************************************
 * FFT Microthreaded version
 ****************************************/

    # Comment this out if you don't want the bit reversal.
    .equ DO_BIT_REVERSAL, 1

    .file "fft_mt_u.s"
    .arch ev6
    .text

    .globl main
    .ent main
#
# Input:
#
# $27 = address of first instruction in main
# $10 = M
#
main:
    ldah $29,0($27)       !gpdisp!1
    lda  $29,0($29)       !gpdisp!1     # $29 = GP

    ldah $0, X($29)       !gprelhigh
    lda  $0, X($0)        !gprellow     # $0 = X

/*
 * Fast Fourier Transform.
 *
 * void FFT(complex X[], int M);
 *
 * $0  = X
 * $10 = M = log2(array size)
 */
.equ MAX_M,      16
.equ BLOCK_POST, 32

_FFT:
	ldah    $1, _cos_sin - 16($29)  !gprelhigh
	lda     $1, _cos_sin - 16($1)   !gprellow  # $1 = &_cos_sin[-1]
	
	mov     1,   $3		    # $3 = 1
	itoft   $3,  $f0
	cvtqt   $f0, $f0		# $f0 = 1.0

	# local const int N = (1 << M);
	sll   $3, $10, $9	    # $9 = N
	subq  $9,   0, $4       # $4 = tmp, doubles as token
	sll   $4,   4, $2
	addq  $2,  $0, $2    	# $2 = &X[N]
	
	allocate $6, 0, 0, 0, 0
	setstart $6, $10    	# start = M
    swch
	setlimit $6, 0    		# limit = 0
	negq     1, $7
	setstep  $6, $7
	setblock $6, 2
	cred     $6, _FFT_1
	mov      $6, $31

    .ifdef DO_BIT_REVERSAL
    swch

	allocate $7, 0, 0, 0, 0
	subq     $2, 16, $2
	setlimit $7, $2	    	# limit = &X[N - 1]
    swch
    setblock $7, BLOCK_POST
	setstart $7, $0	    	# start = &X[0]
	setstep  $7, 16	    	# step  = 16
	
							# $R0 = X
	srl     $9,   1, $1	    # $R1 = N / 2
	clr     $2  	        # $R2 = j = 0
	cred    $7, _FFT_POST
	mov     $7, $31
    .endif

	end
#
# END FFT
#	    
    .end main


    .ifdef DO_BIT_REVERSAL
    .ent _FFT_POST
#
# for (i = 0; i < N - 1; i++) {
#
# $GR0 = X
# $GR1 = N / 2
# $SR0 = j
# $LR0 = &X[i]
#
_FFT_POST:
	.registers 2 1 7  0 0 0	    # GR,SR,LR, GF,SF,LF
	
							# $LR0 = &X[i]
	mov $d0, $l1         	# $LR1 = j
	mov $g0, $l2        	# $LR2 = X

	allocate $l3, 0, 0, 0, 0
	cred $l3, _FFT_POST_SWAP
    swch

    # k = N / 2;
    mov $g1, $l4    	    # $l4 = k
    mov $l1, $l5	        # $l5 = j
    
    # while (k - 1 < j) {
    br $31, 2f
1:
    	# j = j - k;
    	subq $l5, $l4, $l5
    	
    	# k = k / 2;
    	srl $l4, 1, $l4
    #
    # }
2:
    subq $l4,   1, $l6
    subq $l6, $l5, $l6
    blt $l6, 1b
    
    # j = j + k;
    addq $l5, $l4, $s0
    
    # sync
    mov $l3, $31
	end
    .end _FFT_POST


    .ent _FFT_POST_SWAP
#
# $GR0 = &X[i];
# $GR1 = j
# $GR2 = X
#
_FFT_POST_SWAP:
	.registers 3 0 2  0 0 4
	sll  $g1,   4, $l0
	addq $l0, $g2, $l0  	# $l0 = &X[j]
	
	# if (i < j) {
	subq $g0, $l0, $l1
	blt $l1, 1f
	end

1:
	# Swap X[i] and X[j]
    ldt $lf0, 0($g0)
    ldt $lf1, 8($g0)
    ldt $lf2, 0($l0)
    ldt $lf3, 8($l0)
    stt $lf0, 0($l0)
    stt $lf1, 8($l0)
    stt $lf2, 0($g0)
    stt $lf3, 8($g0)
	end
    .end _FFT_POST_SWAP
    .endif

    .ent _FFT_1
#
# for (int k = M; k > 0; k--) {
#
# $GR0 = X
# $GR1 = &_cos_sin[-1]
# $GR2 = &X[N]
# $GR3 = 1
# $GF0 = 1.0
# $SR0 = token
# $LR0 = k (M..1)
#
_FFT_1:
	.registers 4 1 5  1 0 4 	# GR,SR,LR, GF,SF,LF
	
    # complex S = {cos(PI/LE2), -sin(PI/LE2)} = _cos_sin[k - 1];
    sll  $l0,   4, $l4
    addq $l4, $g1, $l4
    ldt  $lf0, 0($l4)
    ldt  $lf1, 8($l4)			# $lf0, $lf1 = S

    # complex U = {1.0, 0.0};
    fmov $gf0, $lf2
    fmov $f31, $lf3  	        # $lf2, $lf3 = U
        	
    # int LE  = 1 << k
    # int LE2 = LE / 2
    sll  $g3, $l0, $l2		
    sll  $l2,   4, $l2		    # $l2 = LE * 16
    
    srl  $l2,   1, $l3
	addq $l3, $g0, $l3	    	# $LR3 = &X[LE2]
    mov  $g2, $l1	        	# $LR1 = &X[N]
    
	allocate $l4, 0, 0, 0, 0
	setlimit $l4, $l3	    	# limit = &X[LE2]
	setstart $l4, $g0	    	# start = &X[0]
	setstep  $l4, 16	    	# step  = 16
	
    mov  $g0,  $l0;		        # $l0 = X
	cpys $lf0, $lf1, $f31;		# Wait for S
    
    mov  $d0, $31	        	# wait for token
	cred $l4, _FFT_2
	mov  $l4, $s0	        	# sync and write token
	end
    .end _FFT_1


    .ent _FFT_2
#	
# for (j = 0; j < LE2; j++) {
#
# $GF0, $GF1 = S
# $SF0, $SF1 = U
# $GR0 = X
# $GR1 = &X[N]
# $GR2 = LE * 16
# $GR3 = &X[LE2]
# $LR0 = &X[j]
#
_FFT_2:
	.registers 4 0 2  2 2 6 	# GR,SR,LR, GF,SF,LF

	allocate $l1, 0, 0, 0, 0
	setstart $l1, $l0		# start = &X[j];
	setlimit $l1, $g1		# limit = &X[N];
	setstep  $l1, $g2		# step = LE * 16;
	
	srl  $g2, 1, $l0		# $l0 = LE2 * 16 (= LE * 16 / 2)
	fmov $df0, $lf0
	fmov $df1, $lf1 		# $lf0, $lf1 = U
	
	cred $l1, _FFT_3
	
	# U = U * S;
	mult $lf0, $gf0, $lf2		# $lf2 = U.re * S.re
	mult $lf1, $gf1, $lf3		# $lf3 = U.im * S.im
	mult $lf1, $gf0, $lf4		# $lf4 = U.im * S.re
	mult $lf0, $gf1, $lf5		# $lf5 = U.re * S.im
	subt $lf2, $lf3, $lf2		# U.re = U.re * S.re - U.im * S.im;
	addt $lf4, $lf5, $lf3		# U.im = U.im * S.re + U.re * S.im;
	cpys $lf2, $lf2, $sf0
	cpys $lf3, $lf3, $sf1

	# sync
	mov $l1, $31
	end
    .end _FFT_2


    .ent _FFT_3
#	
# for (i = j; i < N; i += LE) {
#
# $GF0, $GF1 = U
# $GR0 = LE2 * 16
# $LR0 = &X[i]
#
_FFT_3:
	.registers 1 0 2  2 0 6 	# GR,SR,LR, GF,SF,LF
	
	ldt $lf0, 0($l0)
	ldt $lf1, 8($l0)			# $lf0, $lf1 = X[i]
	
	addq $l0, $gr0, $l1	    	# $l1 = &X[ip] = &X[i + LE2];
	ldt $lf2, 0($l1)
	ldt $lf3, 8($l1)			# $lf2, $lf3 = X[ip]
				
	# complex T = X[i] + X[ip];
	addt $lf0, $lf2, $lf4		# $lf4 = X[i].re + X[ip].re
	addt $lf1, $lf3, $lf5		# $lf5 = X[i].im + X[ip].im

	# X[i] = T;
	stt $lf4, 0($l0)
	stt $lf5, 8($l0)

	# # X[ip] = (X[i] - X[ip]) * U;
	# X[ip].re = (X[i].re - X[ip].re) * U.re - (X[i].im - X[ip].im) * U.im;
	# X[ip].im = (X[i].im - X[ip].im) * U.re + (X[i].re - X[ip].re) * U.im;
				
	subt $lf0, $lf2, $lf4		# $lf4 = X[i].re - X[ip].re
	subt $lf1, $lf3, $lf5		# $lf5 = X[i].im - X[ip].im
				
	mult $lf4, $gf0, $lf2
	mult $lf5, $gf1, $lf3
	subt $lf2, $lf3, $lf2
	stt  $lf2, 0($l1)

	mult $lf5, $gf0, $lf2
	mult $lf4, $gf1, $lf3
	addt $lf2, $lf3, $lf2
	stt  $lf2, 8($l1)
				
	end
    .end _FFT_3
	    
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
    .align 6
    .globl _cos_sin
_cos_sin:
    .include "fft_lookup_u.s"

/*
 * The input and output array
 */
    .align 6
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
