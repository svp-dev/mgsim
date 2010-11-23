    #
    # sin(x), Microthreaded version
    #
    .file "sine_mt_u.s"
    .set noat
    .arch ev6
    .text

    # Don't use more than 9 iterations or the factorial will overflow!
    .equ TAYLOR_ITERATIONS, 9

    .ent main
    .globl main
main:
	allocate/s $31, $2
	
    # Calculate the sine of 2 (in radians)
    addq    $31, 2, $0
	itoft   $0, $f0
	cvtqt   $f0, $f0

	# sin(x)
	setstart $2, 2; swch
	setlimit $2, (TAYLOR_ITERATIONS + 1) * 2
	setstep  $2, 2
	setblock $2, 2
	cred    $2, sin
	puts    1,   $2, 0      # $d0  = 1 (factorial)
	fputg   $f0, $2, 0      # $gf0 = x
	fputs   $f0, $2, 0      # $df0 = x (iteration)
	fputs   $f0, $2, 1      # $df1 = x (power series)
	sync    $2, $0
	mov     $0, $31; swch
	fgets   $2, 0, $f0      # $sf0
	fmov    $f0, $f31
	release $2
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
	.registers 0 1 3 1 2 4		# GR,SR,LR, GF,SF,LF	
sin:
	bis     $l0, 1,  $l2
	sll     $l2, 62, $l2

	# $s0 = factorial(j)
	mulq    $d0, $l0, $l1; swch
	addq    $l0, 1, $l0
	mulq    $l1, $l0, $l0
	mov     $l0, $s0
	
	# $s1 = pow(x,j)
	mult    $df1, $gf0, $lf0; swch
	mult    $lf0, $gf0, $lf3; swch
	fmov    $lf3, $sf1; swch
	
	# $lf0 = (float)$s1;
	itoft   $l0,  $lf0
	cvtqt   $lf0, $lf0
	
	# Negate $lf0 on every other iteration
	itoft   $l2, $lf1
	fneg    $lf0, $lf2
	fcmovlt $lf1, $lf2, $lf0
	
	divt    $lf3, $lf0, $lf0
	addt    $df0, $lf0, $lf0; swch
	fmov    $lf0, $sf0
	end
	.end sin
