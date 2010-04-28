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
	allocate $31, $2

    # Calculate the sine of 2 (in radians)
    mov     2, $0
	itoft   $0, $f0
	cvtqt   $f0, $f0         # $f0 = x
	mult    $f0,$f0,$f1 	 # $f1 = x**2

	# sin(x)
	setstart $2, 2; swch
	setlimit $2, (TAYLOR_ITERATIONS + 1) * 2
	setstep  $2, 2
	setblock $2, 1
	cred    $2, sin
	puts    1,   $2, 0      # $d0  = 1 (factorial)
	fputg   $f1, $2, 0      # $gf0 = x**2
	fputs   $f0, $2, 0      # $df0 = x (iteration)
	fputs   $f0, $2, 1      # $df1 = x (power series)
	sync    $2, $1
	mov     $1, $31; swch
	fgets   $2, 0, $f0      # $sf0
	release $2
	fmov    $f0, $f31       # sync on read back
	end
	.end main
	
	
	.ent sin
    # [in]  $gf0 = x**2
    # [in]  $d0  = factorial[i-1]
    # [in]  $df0 = iter[i-1]
    # [in]  $df1 = pow_x[i-1]
    # [out] $s0  = factorial[i]
    # [out] $sf0 = iter[i]
    # [out] $sf1 = pow_x[i]
	.registers 0 1 3 1 2 4		# GR,SR,LR, GF,SF,LF	
sin:
	mult    $df1, $gf0, $lf3; swch
	bis     $l0,  1,    $l2
	sll     $l2,  62,   $l2
	mulq    $d0,  $l0,  $l1; swch
	addq    $l0,  1,    $l0
	mulq    $l1,  $l0,  $l0
	mov     $l0,  $s0
	fmov    $lf3, $sf1; swch

	itoft   $l0,  $lf0
	cvtqt   $lf0, $lf0
	itoft   $l2,  $lf1
	fneg    $lf0, $lf2
	fcmovlt $lf1, $lf2, $lf0
	divt    $lf3, $lf0, $lf0
	addt    $df0, $lf0, $lf0; swch
	fmov    $lf0, $sf0
	end
	.end sin
