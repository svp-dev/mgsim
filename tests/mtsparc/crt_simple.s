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
        
        ! call test()
        call    test, 0
         nop

        ! exit with code in MGSim
        st      %o0, [0x26c]
        end
        
        .size   _start, .-_start

