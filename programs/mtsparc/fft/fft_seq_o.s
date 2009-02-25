/****************************************
 * FFT Sequential version
 ****************************************/

    # Comment this out if you don't want the bit reversal.
    .equ DO_BIT_REVERSAL, 1

    .file "fft_seq_o.s"
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


#
# Fast Fourier Transform.
#
# void FFT(complex X[], int M);
#
# $0  = X
# $10 = M
#
    .equ MAX_M, 16

_FFT:
    mov   1,    $3
	itoft $3,   $f10
    cvtqt $f10, $f10		# $f10 = 1.0
	ldah  $2, _cos_sin - 16($29)    !gprelhigh
	lda   $2, _cos_sin - 16($2)	    !gprellow   # $2 = &_cos_sin[-1]
	
	# local const int N = (1 << M);
	sll $3, $10, $1 		# $1 = N
	sll $1,   4, $1	    	# $1 = N * 16
	
	# for (int k = M; k > 0; k--) {
1:      					# $10 = k
	
	    # local int LE  = 1 << k;
	    # local int LE2 = LE / 2;
	    addq $10, 4, $5
	    sll  $3, $5, $4		# $4 = LE  * 16
	    srl  $4,  1, $5		# $5 = LE2 * 16 (= LE * 8)
	    
	    # local complex S = {cos(PI/LE2), -sin(PI/LE2)};
	    sll  $10, 4, $6
	    addq $6, $2, $6
	    ldt  $f0, 0($6)
	    ldt  $f1, 8($6)		# $f0, $f1 = S = _cos_sin[k - 1]
	    	
	    # shared complex U = {1.0, 0.0};
	    fmov $f10, $f2
	    fclr $f3       	# $f2, $f3 = U

	    # for (j = 0; j < LE2; j++) {
	    clr $6          	# $6 = j
2:

			# for (i = j; i < N; i += LE) {
		    mov $6, $7	    # $7 = i
3:
			
				addq $7, $5, $8 	# $8 = ip
				addq $8, $0, $8	    # $8 = &X[ip]

				ldt $f6, 0($8)
				ldt $f7, 8($8)		# $f6, $f7 = X[ip]

				addq $7, $0, $9		# $9 = &X[i]
				
				ldt $f4, 0($9)
				ldt $f5, 8($9)		# $f4, $f5 = X[i]
				
				subt $f4, $f6, $f11	# $f11 = X[i].re - X[ip].re
				subt $f5, $f7, $f12	# $f12 = X[i].im - X[ip].im
				addt $f4, $f6, $f8
				addt $f5, $f7, $f9	# $f8, $f9 = T = X[i] + X[ip]

				mult $f11, $f2, $f6
				mult $f12, $f3, $f7
				subt $f6,  $f7, $f6

				stt $f8, 0($9)
				stt $f9, 8($9)
				stt $f6, 0($8)
			
				mult $f12, $f2, $f6
				mult $f11, $f3, $f7
				addt $f6,  $f7, $f6
				addq $7,   $4,  $7

				stt $f6, 8($8)
				
			#
			# }
			subq $1, $7, $8
			bgt  $8, 3b
			
			# $f0, $f1 = S
			# $f2, $f3 = U

			mult $f2, $f0, $f4	# $f4 = U.re * S.re
			mult $f3, $f1, $f5	# $f5 = U.im * S.im
			mult $f3, $f0, $f6	# $f6 = U.im * S.re
			mult $f2, $f1, $f7	# $f7 = U.re * S.im

		    addq $6, 16, $6
		    
			subt $f4, $f5, $f2
			addt $f6, $f7, $f3
		#    
	    # }
		subq $5, $6, $7
        bne  $7, 2b

	#
	# }
    subq $10, 1, $10
    bne $10, 1b
    
    .ifdef DO_BIT_REVERSAL
    # j = 0;
    clr $6	        # $6 = j
    srl $1, 4, $1;	# $1 = N
    
    # for (i = 0; i < N - 1; i++) {
    clr $7      	# $7 = i
4:
    
    	# if (i < j) {
    	subq $7, $6, $4
    	bge $4, 5f
        	# // Swap X[i] and X[j]
            
            # complex T = X[j];
            sll  $6,  4, $4
            addq $4, $0, $4
            ldt $f0, 0($4)
            ldt $f1, 8($4)
            
            # X[j] = X[i];
            # X[i] = T;
            sll  $7,  4, $5
            addq $5, $0, $5
            ldt $f2, 0($5)
            ldt $f3, 8($5)
            stt $f0, 0($5)
            stt $f1, 8($5)
            stt $f2, 0($4)
            stt $f3, 8($4)

        #
        # }
5:
        # k = N/2;
        srl $1, 1, $4	# $4 = k
        
        # while (k - 1 < j) {
        br $31, 7f
6:
        	# j = j - k;
        	subq $6, $4, $6
        	
        	# k = k / 2;
        	srl $4, 1, $4
        #
        # }
7:
        subq $4,  1, $5
        subq $5, $6, $5
        blt $5, 6b
        
        # j = j + k;
        addq $6, $4, $6
	
	#
	# }
	addq $7,  1, $7
	subq $1, $7, $4
	subq $4,  1, $4
	bne $4, 4b
    .endif

	end
#
# END FFT
#	   
    .end main 

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

