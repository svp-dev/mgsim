# crt_simple.s: this file is part of the Microgrid simulator
#
# Copyright (C) 2011 the Microgrid project.
#
# This program is free software, you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, either version 2
# of the License, or (at your option) any later version.
#
# The complete GNU General Public Licence Notice can be found as the
# `COPYING' file in the root directory.
#

	.section ".text"
        .align 64
        .global _start
        .type _start, #function
        .proc 010
_start:
        ldfp    %o6
        
        sub     %o6, 64, %o6
        clr     %r16
        clr     %r17
        clr     %r18
        clr     %r19
        clr     %r20
        clr     %r21
        clr     %r22
        clr     %r23

        clr %o0
        clr %o1
        clr %o2
        clr %o3
        clr %o4
        clr %o5
        
        ! call test()
        call    test, 0
         nop

        ! did test terminate with code 0?
        cmp     %o0, 0
        be      .Lsuccess
         nop

        ! otherwise, save the code
        ! and print a message
        sethi   %hi(slrt_msg), %o5
        mov     115, %o4
        or      %o5, %lo(slrt_msg), %o5
        mov     %o3, %o2
.Lloop:
        print   %o4, 194       ! print char -> stderr
        swch
        add     %o5, 1, %o5
        ldub    [%o5], %o4
        cmp     %o4, 0
        swch
        bne     .Lloop
         nop

        print   %o0, 130    ! print int -> stderr
        mov     10, %o5     ! NL
        print   %o5, 194    ! print char -> stderr
        
.Lfail:
        ! FIXME: inv ins or something to signal failure
        unimp 10
        b,a .Lfail
        nop

.Lsuccess:
        nop
        end
        
        .size   _start, .-_start

! MESSAGE STRINGS USED IN _start
        
	.section        ".rodata"
        .align 8
        .type slrt_msg, #object
        .size slrt_msg, 21
slrt_msg:
        .asciz  "slrt: main returned "

