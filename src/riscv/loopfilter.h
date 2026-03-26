/*
 * Copyright © 2026, dav1d authors
 * All rights reserved.
 */

#include "src/cpu.h"
#include "src/loopfilter.h"

/* LCOV_EXCL_START */
decl_loopfilter_sb_fn(BF(dav1d_lpf_v_sb_y, rvv));

static ALWAYS_INLINE void loop_filter_dsp_init_riscv(Dav1dLoopFilterDSPContext *const c) {
    const unsigned flags = dav1d_get_cpu_flags();

    if (!(flags & DAV1D_RISCV_CPU_FLAG_V)) return;

    (void)c;
}
/* LCOV_EXCL_STOP */
