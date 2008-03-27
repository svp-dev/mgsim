// $R1 = N
// Calculate fibonacci(N)
main:

fibo:
	lda  $R0, LOWORD(stack)($R31);
	ldah $R0, HIWORD(stack)($R0);
	
	bis $R31, 3, $R1;
	
	allocate $R2;
	setlimit $R2, 1;
	setblock $R2, 1;
	setregsi $R2, 0, 1, 2;
	cred $R2, fibo_body;
	end;
	
// Calculate fibonacci(N)
// $GR0 = result
// $GR1 = temp// $GR2 = level size
// $LR0 = i (0 .. 1)
// fibonacci(N)
.align 64;
fibo_body:
	.registers 0 1 2  0 0 0;	// GR,SR,LR, GF,SF,LF
	
	subq $LR0, 1, $LR0;
	ble $LR0, stop;
	swch;
	
	addq $GR0, $GR1, $LR5;
	
	bis $R31, $GR0, $LR0;
	sll $GR1,    1, $LR1;

	allocate $LR1;
	setlimit $LR1, $LR0;
	subq $LR0, 1, $LR0;
	setstart $LR1, $LR0;
	setregs $LR1, $R31, $LR0, $F31, $F31;
	setregsi $LR1, 0, 1, 2;
	setblock $LR1, 1;

	bis $R31, $R31, $LR0;	
	cred $LR1, fibo;
	bis $R31, $LR1, $R31;
	swch;
	addq $DR0, $LR0, $SR0;
	end;
	*/
	
stop:
	addq $LR0, 1, $SR0;
	end;

stack:
	
