/*
 * Copyright © 2026, dav1d authors
 * All rights reserved.
 */

#include "src/riscv/asm.S"

function lpf_v_sb_y_8bpc_rvv, export=1, ext=v
    # Arguments:
    # a0 = dst, a1 = stride, a2 = mask, a3 = lvl, a4 = lvl_stride, a5 = lut, a6 = w

    # 1. Setup RVV to process 8 elements at a time (e.g., 4 left, 4 right of edge)
    #    e8 = 8-bit elements, m1 = 1 vector register, ta = tail agnostic, ma = mask agnostic
    li t0, 8
    vsetvli t1, t0, e8, m1, ta, ma

    # Start loading from 'dst - 4' to get the edge pixels
    addi a0, a0, -4

1:
    # Load 8 pixels across the vertical edge
    vle8.v v0, (a0)

    # --- Filter logic will go here in Phase 2 ---

    # Store the 8 pixels exactly as they were
    vse8.v v0, (a0)

    # Move dst down to the next row (dst += stride)
    add a0, a0, a1

    # Decrement row counter 'w' and loop if not zero
    addi a6, a6, -1
    bnez a6, 1b

    ret
endfunc
