# crt_simple.s: this file is part of the MGSim framework
#
# Copyright (C) 2013 the MGSim project.
#
# This program is free software, you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, either version 2
# of the License, or (at your option) any later version.
#
# The complete GNU General Public Licence Notice can be found as the
# `COPYING' file in the root directory.
#
        .nodelay
        .section .text
        .align  4
        .proc   _start
        .global _start
        .type   _start, @function
_start:
        # prepare the stack (SP, FP)
        l.movhi r1,hi(_stack+4064)
        l.ori   r1,r1,lo(_stack+4064)
        l.addi  r2,r1,0

        # call main
        l.jal   test # call_value_internal

        # prepare the address of the action register
        l.addi  r3,r0,620        # move immediate I

        # inform MGSim of program termination code
        l.sw    0(r3),r11        # SI store

        # leave time for the store to complete before fetch fails
        l.nop
        l.nop
        l.nop
        l.nop
        l.nop
        l.nop
        l.nop
        .size   _start, .-_start
        .comm   _stack,4096,4
