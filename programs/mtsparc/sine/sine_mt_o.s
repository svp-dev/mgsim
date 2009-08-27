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
    clr      %2
	allocate %2, 0, 0, 0, 0

	! Fill initial values for the family
	mov      1, %1           ! %1  = 1 (factorial)
	fmovs   %f1,%f2     	 ! %f2 = x (iteration)
	fmuls   %f1,%f1,%f1 	 ! %f1 = x**2
	fmovs   %f2,%f3     	 ! %f3 = x (power series)
	fmovs   %f1, %f0         ! sync on mul
	swch
	
	! Set up the family parameters.
	! swch because %2 is the result of allocate.
	setstart %2, 2
	swch
	setlimit %2, TAYLOR_ITERATIONS * 2
	setstep  %2, 2
	setblock %2, 1
	
	! Create the family
	cred    sin, %2
	
	! Sync on the family
	mov     %2, %0
	end
	
    ! [in]  %gf0 = x
    ! [in]  %d0  = factorial[i-1]
    ! [in]  %df0 = iter[i-1]
    ! [in]  %df1 = pow_x[i-1]
    ! [out] %s0  = factorial[i]
    ! [out] %sf0 = iter[i]
    ! [out] %sf1 = pow_x[i]
sin:
    ! Inform the hardware how many registers we require.
    ! The assembler verifies the following instructions against this.
	.registers 0 1 3 1 2 3		! GR,SR,LR, GF,SF,LF

	! Advance the power series by multiplying the previous iteration
	! with x. swch because we read %df1.
	fmuls   %df1, %gf0, %lf0
	swch
	
	! Advance the factorial by multiplying with the index and the
	! index + 1. swch because we read %d0.
	umul    %d0, %l0, %l1
	swch
	add     %l0,   1, %l2
	umul    %l1, %l2, %s0

	! Copy the current power series iteration to the next thread.
	! swch because %lf0 is the result of an FP mult.
	fmovs   %lf0, %sf1
	swch

    ! Convert the factorial to floating point
    sll     %l0, 1, %l2
    set     scratch, %l1
    add     %l1, %l2, %l1
    st      %s0, [%l1]
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
	fdivs   %sf1, %lf0, %lf0
	fadds   %df0, %lf0, %lf0
	swch

	! Output the result (we cannot do FP ops directly to shareds)
	fmovs   %lf0, %sf0
	end

    .data
    .align 64
scratch:
    .skip TAYLOR_ITERATIONS * 4
