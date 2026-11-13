/******************************************************************************
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2026, rvv-dav1d contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "src/riscv/asm.S"

// void dav1d_fill_levelcache_block_rvv(uint8_t (*level_cache_ptr)[2],
//                                      ptrdiff_t b_stride, int bw, int bh,
//                                      unsigned v0, unsigned v1)
//
// 纯广播填充，是 RVV 上最赚的形态：16 位模式在 GPR 里拼好 -> 广播 -> 逐行 vse16.v
//
// RVV 1.0 正确性要点（都是踩过的坑）：
//  - 每个元素是 1 个 16 位 level-cache 条目，一行 bw 个元素（不是 bw*2）
//  - VLEN 无关：不硬编码寄存器字节数，vsetvli 显式 AVL 会自动 clamp 到 VLMAX，
//    指针步长用刚装上的 vl 计算
//  - vsetvli 必须在 store 前，步长用它产出的 vl
//  - 尾策略：模式用 AVL=-1（vl=VLMAX）广播一次，vse16.v 只读 active lane，
//    agnostic 尾巴不会破坏模式
//  - 每行只写 bw*2 字节，stride padding 不碰，与 C 参考一致

function fill_levelcache_block_rvv, export=1, ext=v
    // a0 = level_cache_ptr, a1 = b_stride, a2 = bw, a3 = bh, a4 = v0, a5 = v1

    // 拼 16 位模式：v0 | (v1 << 8)
    andi    t0, a4, 0xff
    andi    t1, a5, 0xff
    slli    t1, t1, 8
    or      t0, t0, t1

    slli    t1, a1, 1              // t1 = 行间字节步长

    // 把 16 位模式广播到 v0（全部 VLMAX 个 lane）；AVL=-1 => vl=VLMAX
    li      t2, -1
    vsetvli zero, t2, e16, m1, tu, ma
    vmv.v.x v0, t0

    blez    a3, .Ldone             // bh <= 0

.Lrow:
    mv      t3, a2                  // 本行剩余元素数
    mv      t4, a0                  // 当前行指针

.Lchunk:
    beqz    t3, .Lrow_done
    vsetvli t5, t3, e16, m1, ta, ma // vl = min(remaining, VLMAX)
    vse16.v v0, (t4)
    slli    t6, t5, 1               // 实际写入字节数
    add     t4, t4, t6
    sub     t3, t3, t5
    j       .Lchunk

.Lrow_done:
    add     a0, a0, t1              // 下一行
    addi    a3, a3, -1
    bnez    a3, .Lrow

.Ldone:
    ret
endfunc
