/*
 * Copyright © 2026, dav1d authors
 * All rights reserved.
 */

#include "src/riscv/asm.S"

function lpf_v_sb_y_8bpc_rvv, export=1, ext=v
    # Arguments:
    # a0 = dst, a1 = stride, a2 = mask, a3 = lvl, a4 = lvl_stride, a5 = lut, a6 = w

1:
    # Process up to 'w' rows in parallel
    vsetvli t1, a6, e8, m1, ta, ma

    # Calculate pointers to columns
    # dst (a0) points to q0
    addi t2, a0, -4
    vlse8.v v0, (t2), a1  # load column p3
    addi t2, a0, -3
    vlse8.v v1, (t2), a1  # load column p2
    addi t2, a0, -2
    vlse8.v v2, (t2), a1  # load column p1
    addi t2, a0, -1
    vlse8.v v3, (t2), a1  # load column p0

    vlse8.v v4, (a0), a1  # load column q0
    addi t2, a0, 1
    vlse8.v v5, (t2), a1  # load column q1
    addi t2, a0, 2
    vlse8.v v6, (t2), a1  # load column q2
    addi t2, a0, 3
    vlse8.v v7, (t2), a1  # load column q3

    # --- Filter logic will go here in phase 2 ---
    # Calculations like vector p0 - q0 can now be simply:
    # vsub.vv v_diff, v3, v4

    # Store the result (currently just a stub storing unmodified pixels)
    addi t2, a0, -4
    vsse8.v v0, (t2), a1
    addi t2, a0, -3
    vsse8.v v1, (t2), a1
    addi t2, a0, -2
    vsse8.v v2, (t2), a1
    addi t2, a0, -1
    vsse8.v v3, (t2), a1

    vsse8.v v4, (a0), a1
    addi t2, a0, 1
    vsse8.v v5, (t2), a1
    addi t2, a0, 2
    vsse8.v v6, (t2), a1
    addi t2, a0, 3
    vsse8.v v7, (t2), a1

    # Advance a0 down by (vl * stride) rows
    mul t2, t1, a1
    add a0, a0, t2

    # Decrement 'w' by the number of elements processed
    sub a6, a6, t1
    bnez a6, 1b

    ret
endfunc
