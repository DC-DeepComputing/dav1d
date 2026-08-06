/******************************************************************************
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2026, dav1d authors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "src/riscv/asm.S"

function lpf_v_sb_y_8bpc_rvv, export=1, ext=v
    # Arguments:
    # a0 = dst, a1 = stride, a2 = vmask, a3 = lvl, a4 = lvl_stride, a5 = lut, a6 = w

    # Save required registers (using caller-saved t-registers for temp logic)
    # t0 = vmask[0], t1 = vmask[1], t2 = vmask[2]
    lw t0, 0(a2)
    lw t1, 4(a2)
    lw t2, 8(a2)

    # vm = vmask[0] | vmask[1] | vmask[2]
    or t3, t0, t1
    or t3, t3, t2
    beqz t3, .Lend   # if (!vm) return

    # x = 1 (column iterator bitmask)
    li t4, 1

.Lloop_x:
    # Check if vm & ~(x - 1) == 0 (early exit if no more bits set)
    addi t5, t4, -1
    not t5, t5
    and t5, t3, t5
    beqz t5, .Lend

    # Check if vm & x
    and t5, t3, t4
    beqz t5, .Lnext_x

    # Load L = l[0][0] ? l[0][0] : l[-b4_stride][0]
    lbu t5, 0(a3)
    bnez t5, .Lhas_L
    sub t6, a3, a4    # l - b4_stride
    lbu t5, 0(t6)
    beqz t5, .Lnext_x # if (!L) continue
.Lhas_L:

    # Calculate H = L >> 4
    srli a7, t5, 4

    # Calculate E = lut->e[L], I = lut->i[L]
    # lut structure: uint8_t e[64], i[64];
    add t6, a5, t5
    lbu t5, 0(t6)     # E
    lbu t6, 64(t6)    # I

    # Determine wd: idx = (vmask[2] & x) ? 2 : !!(vmask[1] & x)
    and t5, t2, t4    # vmask[2] & x
    bnez t5, .Lidx_2
    and t5, t1, t4    # vmask[1] & x
    snez t5, t5       # 1 if vmask[1] & x, else 0
    j .Lidx_done
.Lidx_2:
    li t5, 2
.Lidx_done:
    # wd = 4 << idx
    li t6, 4
    sllw a7, t6, t5   # Now a7 holds wd. (E, I, H are in vector logic)

    # ==== INNER VECTOR BLOCK (Processing 4 columns) ====
    # Process exactly 4 columns for this specific E, I, H
    vsetivli zero, 4, e8, m1, ta, ma

    # Calculate row pointers (dst points to row q0)
    sub t5, a0, a1  # p0
    sub t6, t5, a1  # p1
    # ... more pointers can be calculated here based on wd

    # Load rows for p1, p0, q0, q1
    vle8.v v1, (t6)
    vle8.v v0, (t5)
    vle8.v v2, (a0)
    add t5, a0, a1
    vle8.v v3, (t5)

    # --- Filter logic computations (Phase 2 core math) ---
    # abs(p1 - p0) <= I, abs(p0 - q0)*2 + ... <= E
    # (Left as a stub to be filled in with complete filter equations)

    # Store backward (currently unmodified pixels)
    sub t5, a0, a1
    sub t6, t5, a1
    vse8.v v1, (t6)
    vse8.v v0, (t5)
    vse8.v v2, (a0)
    add t5, a0, a1
    vse8.v v3, (t5)

.Lnext_x:
    # Advance x by shifting left
    slli t4, t4, 1
    # dst += 4
    addi a0, a0, 4
    # l++ (lvl map iterates by 1 byte per 4-pixel column)
    addi a3, a3, 1
    j .Lloop_x

.Lend:
    ret
endfunc
