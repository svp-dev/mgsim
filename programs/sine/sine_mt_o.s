    #
    # sin(x), Microthreaded version
    #
    .file "sine_mt_o.s"
    .arch ev6
    .text

    # Don't use more than 9 iterations or the factorial will overflow!
    .equ TAYLOR_ITERATIONS, 9

    .ent main
    .globl main
main:
	allocate $2

    # Calculate the sine of 2 (in radians)
    mov     2, $0
	itoft   $0, $f0
	cvtqt   $f0, $f0

	# sin(x)
	mov     1, $0  	         # $0 = 1 (factorial)
	cpys    $f0,$f0,$f1 	 # $f1 = x (iteration)
	mult    $f0,$f0,$f0 	 # $f0 = x**2
	cpys    $f1,$f1,$f2 	 # $f2 = x (power series)
	cpys    $f0, $f0, $f31   # sync on mul
	setstart $2, 2; swch
	setlimit $2, (TAYLOR_ITERATIONS + 1) * 2
	setstep  $2, 2
	setblock $2, 1
	cred    $2, sin
	mov     $2, $31
	end
	.end main
	
	
	.ent sin
    # [in]  $gf0 = x
    # [in]  $d0  = factorial[i-1]
    # [in]  $df0 = iter[i-1]
    # [in]  $df1 = pow_x[i-1]
    # [out] $s0  = factorial[i]
    # [out] $sf0 = iter[i]
    # [out] $sf1 = pow_x[i]
sin:
	.registers 0 1 3 1 2 3		# GR,SR,LR, GF,SF,LF
	
	mult    $df1, $gf0, $lf0; swch
	bis     $l0,  1,    $l2
	sll     $l2,  62,   $l2
	mulq    $d0,  $l0,  $l1; swch
	addq    $l0,  1,    $l0
	mulq    $l1,  $l0,  $s0
	cpys    $lf0, $lf0, $sf1; swch

	itoft   $s0,  $lf0
	cvtqt   $lf0, $lf0
	itoft   $l2,  $lf1
	cpysn   $lf0, $lf0, $lf2
	fcmovlt $lf1, $lf2, $lf0
	divt    $sf1, $lf0, $lf0
	addt    $df0, $lf0, $lf0; swch
	cpys    $lf0, $lf0, $sf0
	end
	.end sin
