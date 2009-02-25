    #
    # sin(x), Sequential version
    #
    .file "sine_seq_u.s"
    .arch ev6
    .text

    # Don't use more than 9 iterations or the factorial will overflow!
    .equ TAYLOR_ITERATIONS, 9

    .ent main
    .globl main
main:
    # Calculate the sine of 2 (in radians)
	mov     2, $0
	itoft   $0, $f0
	cvtqt   $f0, $f0

    # [in]  $f0  = x
    # [out] $f0  = sin(x)
sin:
	# Initialize variables
	mov     1, $0	        # $0  = 1
	mov     1, $1	        # $1  = 1 (factorial)
	cpys    $f0, $f0, $f1   # $f1 = x (power series)
	cpys    $f0, $f0, $f2	# $f2 = x
	itoft   $0, $f4
	cvtqt   $f4, $f4		# $f4 = 1.0;
	
	mov TAYLOR_ITERATIONS, $2
	br $31, 2f
1:
		addq    $0, 1, $0
		mulq    $1, $0, $1
		addq    $0, 1, $0
		mulq    $1, $0, $1
		
		mult    $f1, $f2, $f1
		mult    $f1, $f2, $f1
		
		# $f3 = (float)$r1;
		itoft   $r1, $f3
		cvtqt   $f3, $f3

		# Negate $F3 on every other iteration
		cpysn   $f4, $f4, $f4
		cpysn   $f3, $f3, $f5
		fcmovlt $f4, $f5, $f3
		
		# $F3 = $F1 / $F3;
		divt    $f1, $f3, $f6
		addt    $f0, $f6, $f0
	
		subq $2, 1, $2
2:
	bne $2, 1b
	end
    .end main
