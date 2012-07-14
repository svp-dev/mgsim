	# This test does a bundle creation test.
    .file "ceb_a.s"
    .set noat
    .arch ev6
    .text
    
    .ent main
    .globl main   
main:
    ldpc    $27
    ldgp    $gp,0($27)
    ldfp 	$1
    lda  	$2,bar
    mov  	10,$3
    mov  	3, $4
    mov  	2, $5
    subq        $1,64,$1
    stq   	$4,0($1)
    stq   	$2,8($1)
    stq   	$5,16($1)
    creba/a 	$1,$3
    end
    .end main
     
    .ent bar
    .registers 0 1 0 0 0 0
bar: 
	 addq $d0,10,$s0
   end
   .end bar
   
   .section .rodata
   	.ascii "PLACES: 2\0"
   
