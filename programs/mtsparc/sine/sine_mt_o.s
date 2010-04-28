    !
    ! sin(x), Microthreaded version
    !
    .file "sine_mt_o.s"

    ! Don't use more than 5 iterations or the factorial will overflow!
    .equ TAYLOR_ITERATIONS, 5
    
    .section ".rodata"
    ! Calculate the sine of 2 (in radians)
input:
    .float 2

    .text
    .globl main
main:
    set     input, %1
    ld      [%1], %f1

    ! Request a family table entry
	allocate %0, %2

	! Set up the family parameters.
	! swch because %2 is the result of allocate.
	setstart %2, 2
	swch
	setlimit %2, TAYLOR_ITERATIONS * 2
	setstep  %2, 2
	setblock %2, 1
	
	! Create the family
	cred    sin, %2
	
	! Fill initial values for the family
	mov      1,  %1
	puts    %1,  %2, 0       ! %d0 = 1 (factorial)
	fputs   %f1, %2, 0     	 ! %df0 = x (iteration)
	fputs   %f1, %2, 1     	 ! %df1 = x (power series)
	fmuls   %f1, %f1, %f1 	 ! %f1 = x**2
	fputg   %f1, %2, 0       ! %gf0 = x * x
	swch
	
	! Sync on the family
	sync    %2, %1
	mov     %1, %0
	fgets   %2, 0, %f1
	release %2
	end
	
    ! [in]  %gf0 = x*x
    ! [in]  %d0  = factorial[i-1]
    ! [in]  %df0 = iter[i-1]
    ! [in]  %df1 = pow_x[i-1]
    ! [out] %s0  = factorial[i]
    ! [out] %sf0 = iter[i]
    ! [out] %sf1 = pow_x[i]
    
    ! Inform the hardware how many registers we require.
    ! The assembler verifies the following instructions against this.
    .registers 0 1 4 1 2 3		! GR,SR,LR, GF,SF,LF
sin:
	! Advance the power series by multiplying the previous iteration
	! with x. swch because we read %df1.
	fmuls   %df1, %gf0, %lf2
	swch
	
	! Advance the factorial by multiplying with the index and the
	! index + 1. swch because we read %d0.
	umul    %d0, %l0, %l1
	swch
	add     %l0,   1, %l2
	umul    %l1, %l2, %l3
	mov     %l3, %s0

	! Copy the current power series iteration to the next thread.
	! swch because %lf0 is the result of an FP mult.
	fmovs   %lf2, %sf1
	swch

    ! Convert the factorial to floating point
    sll     %l0, 1, %l2
    set     scratch, %l1
    add     %l1, %l2, %l1
    st      %l3, [%l1]
    ld      [%l1], %lf0
    fitos   %lf0, %lf0	    ! %lf0 = (float)factorial[i]
    swch
    
	! If the index (div 2 == iteration) is odd, negate the factorial.
	! swch because we branch
	btst    2, %l0
	be      1f
	swch
	fnegs   %lf0, %lf0
1:
    ! Divide the current power series iteration with the (sign-adjusted)
    ! factorial and add the result to the running taylor series.
    ! swch because we read %df0 and %lf0 is the result of an FP div.
	fdivs   %lf2, %lf0, %lf0
	fadds   %df0, %lf0, %lf0
	swch

	! Output the result (we cannot do FP ops directly to shareds)
	fmovs   %lf0, %sf0
	end

    .data
    .align 64
scratch:
    .skip TAYLOR_ITERATIONS * 4
