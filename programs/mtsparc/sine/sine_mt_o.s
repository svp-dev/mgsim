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
	allocate %0, %2         ! Default

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
	puts    %1,  %2, 0       ! %td0 = 1 (factorial)
	fputs   %f1, %2, 0     	 ! %tdf0 = x (iteration)
	fputs   %f1, %2, 1     	 ! %tdf1 = x (power series)
	fmuls   %f1, %f1, %f1 	 ! %f1 = x**2
	fputg   %f1, %2, 0       ! %tgf0 = x * x
	swch
	
	! Sync on the family
	sync    %2, %1
	mov     %1, %0
	fgets   %2, 0, %f1
	release %2
	end
	
    ! [in]  %tgf0 = x*x
    ! [in]  %td0  = factorial[i-1]
    ! [in]  %tdf0 = iter[i-1]
    ! [in]  %tdf1 = pow_x[i-1]
    ! [out] %ts0  = factorial[i]
    ! [out] %tsf0 = iter[i]
    ! [out] %tsf1 = pow_x[i]
    
    ! Inform the hardware how many registers we require.
    ! The assembler verifies the following instructions against this.
    .registers 0 1 4 1 2 3		! GR,SR,LR, GF,SF,LF
sin:
	! Advance the power series by multiplying the previous iteration
	! with x. swch because we read %tdf1.
	fmuls   %tdf1, %tgf0, %tlf2
	swch
	
	! Advance the factorial by multiplying with the index and the
	! index + 1. swch because we read %td0.
	umul    %td0, %tl0, %tl1
	swch
	add     %tl0,   1, %tl2
	umul    %tl1, %tl2, %tl3
	mov     %tl3, %ts0

	! Copy the current power series iteration to the next thread.
	! swch because %tlf0 is the result of an FP mult.
	fmovs   %tlf2, %tsf1
	swch

    ! Convert the factorial to floating point
    sll     %tl0, 1, %tl2
    set     scratch, %tl1
    add     %tl1, %tl2, %tl1
    st      %tl3, [%tl1]
    ld      [%tl1], %tlf0
    fitos   %tlf0, %tlf0	    ! %tlf0 = (float)factorial[i]
    swch
    
	! If the index (div 2 == iteration) is odd, negate the factorial.
	! swch because we branch
	btst    2, %tl0
	be      1f
	swch
	fnegs   %tlf0, %tlf0
1:
    ! Divide the current power series iteration with the (sign-adjusted)
    ! factorial and add the result to the running taylor series.
    ! swch because we read %tdf0 and %tlf0 is the result of an FP div.
	fdivs   %tlf2, %tlf0, %tlf0
	fadds   %tdf0, %tlf0, %tlf0
	swch

	! Output the result (we cannot do FP ops directly to shareds)
	fmovs   %tlf0, %tsf0
	end

    .data
    .align 64
scratch:
    .skip TAYLOR_ITERATIONS * 4
