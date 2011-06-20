/* This test does a bundle creation test.*/
    .file "bundle.s"
    .set noat
    .text

    .globl main
    .ent main
main:
    ldpc $27
    ldgp $gp,0($27)
    ldfp $1
    lda  $2,bar
    mov  10,$3
    mov  3, $4
    mov  2, $5
    stq  $4,-32($1)
    stq  $2,-24($1)
    stq  $5,-16($1)
    subq $1,32,$1
    crebi $3,$1
    
    end    
   .end main
    
    .ent bar
    .registers 0 1 0 0 0 0
bar:		addl $d0,10,$s0 
				end   
				.end bar
   
   	.data
   	.ascii "PLACES: 2\0"
   