/****************************************
 * FFT Microthreaded version
 ****************************************/

    # Comment this out if you don't want the bit reversal.
    # .equ DO_BIT_REVERSAL, 1

    .file "fft_mt_o.s"
    .arch ev4
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
    lda  $0, X($0)        !gprellow	    # $0 = X

/*
 * Fast Fourier Transform.
 *
 * void FFT(complex X[], int M);
 *
 * $0  = X
 * $10 = M = log2(array size);
 */
.equ MAX_M,      16
.equ BLOCK_POST, 16

_FFT:
	mov     1, $3	        # $3 = 1
	sll     $3, $10, $9	    # $9 = N
	subq    $9,   1, $5	    # $5 = N - 1	(doubles as token)
	srl     $9,  1, $1	    # $1 = N / 2

    
    .ifdef DO_BIT_REVERSAL	
	allocate $7, 0, 0, 0, 0
	addq     $0, 16, $8
	setstart $7, $8		    # start = &X[1]
	sll      $5,  4, $8
	addq	 $8, $0, $8
	setlimit $7, $8 		# limit = &X[N - 1]
	setstep  $7, 16 		# step  = 16
	setblock $7, BLOCK_POST
	
	mov     $1, $2	        # $2 = j = N / 2
	cred    $7, _FFT_POST
	mov     $7, $31
	swch
	.endif
	
	allocate $7, 0, 0, 0, 0

	sll     $1,   4, $1		                # $1 = (N / 2) * 16;
	ldah    $2, _cos_sin($29)   !gprelhigh
	lda     $2, _cos_sin($2)    !gprellow	# $2 = _cos_sin
    sll     $3, MAX_M, $4                   # $4 = MAX_N

	# create and sync
	setstart $7, $3		# start = 1
	addq     $10, 1, $10
	setlimit $7, $10	# limit = M + 1
	setplace $7, $31    # place = LOCAL
	cred    $7, _FFT_1
	mov     $7, $31
	end
#
# END FFT
#
    .end main

    .ifdef DO_BIT_REVERSAL
#
# for (i = 0; i < N - 1; i++) {
#
# $g0 = X
# $g1 = N / 2
# $s0 = j
# $l0 = &X[i]
#
    .ent _FFT_POST
_FFT_POST:
	.registers 2 1 7  0 0 0	    # GR,SR,LR, GF,SF,LF
	
	allocate $l3, 0, 0, 0, 0
								# $l0 = &X[i]
	mov $d0, $l1; swch	        # $l1 = j
	mov $g0, $l2		        # $l2 = X
	cred $l3, _FFT_POST_SWAP
	
    # k = N / 2;
    mov $g1, $l4		# $l4 = k
    mov $l1, $l5		# $l5 = j
    
    # while (k <= j) {
    br $31, 2f
1:
    	# j = j - k;
    	subq $l5, $l4, $l5
    	
    	# k = k / 2;
    	srl $l4, 1, $l4
    #
    # }
2:
    subq $l4, $l5, $l6
    ble $l6, 1b; swch
    
    # j = j + k;
    addq $l5, $l4, $s0
    
    # sync
    mov $l3, $31
	end
    .end _FFT_POST


    .ent _FFT_POST_SWAP
    #
    # $g0 = &X[i];
    # $g1 = j
    # $g2 = X
    #
_FFT_POST_SWAP:
	.registers 3 0 2  0 0 4
	sll  $g1,   4, $l0
	addq $l0, $g2, $l0	# $l0 = &X[j]
	
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
    stt $lf0, 0($l0); swch
    stt $lf1, 8($l0); swch
    stt $lf2, 0($g0); swch
    stt $lf3, 8($g0)
	end
    .end _FFT_POST_SWAP
    .endif

/*
 * for (int k = 1; k <= M; k++) {
 *
 * $GR0 = X
 * $GR1 = (N / 2) * 16
 * $GR2 = _cos_sin
 * $GR3 = 1
 * $GR4 = MAX_N
 * $SR0 = token
 * $LR0 = k
 */
    .ent _FFT_1
_FFT_1:
	.registers 5 1 6  0 0 0	    # GR,SR,LR, GF,SF,LF
	
	allocate $lr5, 0, 0, 0, 0	# start = 0
	setlimit $lr5, $gr1		    # limit = (N / 2) * 16
	setstep  $lr5, 16			# step  = 16
	# setblock $lr5, $gr5
	
	srl  $gr4, $lr0, $lr4		# $LR4 = Z = (MAX_N >> k);
    mov  $gr0, $lr3		        # $LR3 = X
    mov  $gr2, $lr2		        # $LR2 = _cos_sin
	subq $lr0,    1, $lr1		# $LR1 = k - 1;
	sll  $gr3, $lr0, $lr0
	sll  $lr0,    3, $lr0
	subq $lr0,    1, $lr0		# $LR0 = LE2 * 16 - 1
	
    mov  $dr0, $31; swch        # wait for token
	cred $lr5, _FFT_2
	mov  $lr5, $sr0 		    # sync and write token
	end;
    .end _FFT_1

/*
 * for (i = 0; i < N / 2; i++) {
 *
 * $GR0 = LE2 * 16 - 1
 * $GR1 = k - 1
 * $GR2 = _cos_sin
 * $GR3 = X
 * $GR4 = Z
 * $LR0 = i * 16
 */
    .ent _FFT_2
_FFT_2:
	.registers 5 0 3  0 0 8	    # GR,SR,LR, GF,SF,LF
	
	and  $lr0, $gr0, $lr1	    # $LR1 = w
	subq $lr0, $lr1, $lr0
	sll  $lr0,    1, $lr0
	addq $lr0, $lr1, $lr0	    # $LR0 = j
	
	addq $lr0, $gr0, $lr2       # $LR2 = ip;
	addq $lr2, $gr3, $lr2	    # $LR2 = &X[ip] - 1;
	ldt $lf2, 1($lr2)
	ldt $lf3, 9($lr2)		    # $LF2, $LF3 = X[ip]
	
	mulq $lr1, $gr4, $lr1
	addq $lr1, $gr2, $lr1	    # $LR1 = &_cos_sin[w * Z];
	ldt $lf4, 0($lr1)
	ldt $lf5, 8($lr1)		    # $LF4, $LF5 = U
				
	addq $lr0, $gr3, $lr0       # $LR0 = &X[j];
	ldt $lf0, 0($lr0)
	ldt $lf1, 8($lr0)		    # $LF0, $LF1 = X[j]
	
	# complex T = U * X[ip];
	mult $lf4, $lf2, $lf6; swch
	mult $lf5, $lf3, $lf7; swch
	mult $lf5, $lf2, $lf2
	mult $lf4, $lf3, $lf3
	subt $lf6, $lf7, $lf6; swch
	addt $lf2, $lf3, $lf7; swch	# $LF6, $LF7 = T
	
	# X[ip] = X[j] - T
	# X[j]  = X[j] + T
	subt $lf0, $lf6, $lf2; swch
	addt $lf0, $lf6, $lf0
	subt $lf1, $lf7, $lf3; swch
	addt $lf1, $lf7, $lf1
	
	stt $lf2, 1($lr2); swch
	stt $lf3, 9($lr2); swch
	stt $lf0, 0($lr0); swch
	stt $lf1, 8($lr0)
	end
    .end _FFT_2

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
    .align 6
    .globl _cos_sin
_cos_sin:
    .include "fft_lookup_o.s"

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
