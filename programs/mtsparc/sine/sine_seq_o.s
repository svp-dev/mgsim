    #
    # sin(x), Sequential version
    #
    .file "sine_seq_o.s"
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
	mov     1, $0	        # $0 = 1
	mov     1, $1	        # $1 = 1 (factorial)
	cpys    $f0, $f0, $f1	# $f1 = x (power series)
	cpys    $f0, $f0, $f2	# $f2 = x
	itoft   $0, $f4
	cvtqt   $f4, $f4		# $f4 = 1.0
	
	mov TAYLOR_ITERATIONS, $2
	
	# We assume $r2 is not zero, so we don't jump to the test
	br $r31, 1f

    # This loop fits exactly in one cache line, so align it to one
    .align 6
1:
		mult $f1, $f2, $f1
		addq $r0, 1, $r0
		mulq $r1, $r0, $r1
		addq $r0, 1, $r0
		mult $f1, $f2, $f1
		mulq $r1, $r0, $r1
		itoft $r1, $f3
		cvtqt $f3, $f3
		
		cpysn $f4, $f4, $f4
		cpysn $f3, $f3, $f5
		fcmovlt $f4, $f5, $f3
		divt $f1, $f3, $f6
		subq $r2, 1, $r2
		addt $f0, $f6, $f0
	bne $2, 1b
	end
	.end main
