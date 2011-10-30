	! This test does a bundle creation test.
    .file "ceb_a.s"
    .text

    .globl main
main:
    ldfp 	%1
    set  	bar,%2
    mov  	10,%3
    mov  	3, %4
    mov  	2, %5
    st   	%4,[%1-16]
    st   	%2,[%1-12]
    st   	%5,[%1-8]
    sub  	%1,16,%1
    creba 	%1,%3
   end
        
    .registers 0 1 0 0 0 0
bar: 
	 add %td0,10,%ts0
   end
   .section .rodata
   	.ascii "PLACES: 2\0"
   