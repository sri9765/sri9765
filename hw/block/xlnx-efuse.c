/*
 * QEMU model of the EFUSE eFuse
 *
 * Copyright (c) 2015 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
 * Partially autogenerated by xregqemu.py 2015-01-02.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/block/flash.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "qemu/host-utils.h"
#include "hw/sysbus.h"
#include "hw/register-dep.h"
#include "hw/ptimer.h"
#include "sysemu/blockdev.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "sysemu/block-backend.h"
#include "hw/zynqmp_aes_key.h"
#include "hw/block/xlnx-efuse.h"

#ifndef XLNX_EFUSE_ERR_DEBUG
#define XLNX_EFUSE_ERR_DEBUG 0
#endif

bool efuse_get_bit(XLNXEFuse *s, unsigned int bit)
{
    bool b = s->fuse32[bit / 32] & (1 << (bit % 32));
    return b;
}

bool efuse_is_pgm(XLNXEFuse *s)
{
    return s->programming;
}

#ifdef BIT_TEST
static bool efuse_bit_is_test(unsigned int fbit)
{
    unsigned int word = fbit / 32;
    unsigned int lword = word % 32;
    unsigned int bit = fbit % 32;
    bool tst = false;

    if (lword == 0 && bit < 4) {
        tst = true;
    } else if (word == 23) {
        if (bit >= 23) {
            tst = true;
        }
    } else if (word < 23 || word > 31) {

        if (lword == bit) {
            tst = true;
        }
    }

    return tst;
}
#endif

/* Update the u32 array from efuse bits. Slow but simple approach.  */
void efuse_sync_u32(XLNXEFuse *s, uint32_t *u32,
                           unsigned int f_start, unsigned int f_end,
                           unsigned int f_written)
{
    unsigned int fbit, wbits = 0, u32_off = 0;

    /* Avoid working on bits that are not relevant.  */
    if (f_written != FBIT_UNKNOWN
        && (f_written < f_start || f_written > f_end)) {
        return;
    }

    for (fbit = f_start; fbit <= f_end; fbit++, wbits++) {
#ifdef BIT_TEST
        if (efuse_bit_is_test(fbit)) {
            continue;
        }
#endif
        if (wbits == 32) {
            /* Update the key offset.  */
            u32_off += 1;
            wbits = 0;
        }
        u32[u32_off] |= efuse_get_bit(s, fbit) << wbits;
    }
}

static void efuse_sync_bdrv(XLNXEFuse *s)
{
    unsigned int efuse_byte;

    if (!s->blk || s->blk_ro) {
        return;  /* Silient on read-only backend to avoid message flood */
    }

    efuse_byte = s->efuse_idx / 8;

    if (blk_pwrite(s->blk, efuse_byte, ((uint8_t *) s->fuse32) + efuse_byte,
                   1, 0) < 0) {
        error_report("%s: write error in byte %" PRIu32 ".",
                      __func__, efuse_byte);
    }
}

void efuse_pgm_start(XLNXEFuse *s, int tpgm, uint64_t val)
{
    s->efuse_idx = (uint32_t) val;
    ptimer_transaction_begin(s->timer_pgm);
    ptimer_stop(s->timer_pgm);
    /* HW specs say 12us (+- 1us). Real value to be specified when
       real sillicon arrives. We emulate 13us per fuse, 26us with the
       redundancy bits.  */
    ptimer_set_limit(s->timer_pgm, tpgm * 2, 1);
    ptimer_run(s->timer_pgm, 1);
    ptimer_transaction_commit(s->timer_pgm);
    s->programming = true;
}

static void timer_ps_hit(void *opaque)
{
    XLNXEFuse *s = XLNX_EFUSE(opaque);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: FATAL: XLNXEFuse PS held high for more than 500ms!\n",
                  object_get_canonical_path(OBJECT(s)));
}

static void timer_pgm_hit(void *opaque)
{
    XLNXEFuse *s = XLNX_EFUSE(opaque);
    unsigned int efuse_word;

    if (s->programming == false) {
        return;     /* false alarm */
    }

    efuse_word = s->efuse_idx / 32;

    s->fuse32[efuse_word] |= 1 << (s->efuse_idx % 32);
    efuse_sync_bdrv(s);

    s->programming = false;

    if (s->pgm_done) {
        s->pgm_done(s->dev, false);
    }
}

void efuse_stop_timer_ps(XLNXEFuse *s)
{
    ptimer_transaction_begin(s->timer_ps);
    ptimer_stop(s->timer_ps);
    ptimer_transaction_commit(s->timer_ps);
}

void efuse_set_timer_ps(XLNXEFuse *s, int tsu_h_ps)
{
    ptimer_transaction_begin(s->timer_ps);
    ptimer_stop(s->timer_ps);
    ptimer_set_limit(s->timer_ps, tsu_h_ps * 1000, 1);
    ptimer_run(s->timer_ps, 1);
    ptimer_transaction_commit(s->timer_ps);
}

void efuse_pgm_complete(XLNXEFuse *s)
{
    if (s->programming && ptimer_get_count(s->timer_pgm) == 0) {
        /* Programming is ready.  */
        timer_pgm_hit(s);
    }
}

unsigned int efuse_tbits_read(XLNXEFuse *s, int n)
{
    int efuse_start_row_num = (s->efuse_size * n) / 32;
    uint32_t data = s->fuse32[efuse_start_row_num] >> TBIT0_OFFSET;
    return data;
}

static void efuse_realize(DeviceState *dev, Error **errp)
{
    XLNXEFuse *s = XLNX_EFUSE(dev);
    BlockBackend *blk;
    DriveInfo *dinfo;
    unsigned int nr_bytes;
    const char *prefix = object_get_canonical_path(OBJECT(dev));

    dinfo = drive_get_next(IF_PFLASH);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    nr_bytes = (s->efuse_nr * s->efuse_size) / 8;
    s->fuse32 = g_malloc0(nr_bytes);
    if (blk) {
        qdev_prop_set_drive(dev, "drive", blk, NULL);

        s->blk_ro = blk_is_read_only(s->blk);
        if (s->blk_ro) {
            warn_report("%s: update not saved: backstore is read-only",
                        object_get_canonical_path(OBJECT(s)));
        }
        blk_set_perm(s->blk,
                     (BLK_PERM_CONSISTENT_READ
                      | (s->blk_ro ? 0 : BLK_PERM_WRITE)), BLK_PERM_ALL,
                     &error_abort);

        if (blk_pread(s->blk, 0, (void *) s->fuse32, nr_bytes) < 0) {
            error_setg(&error_abort, "%s: Unable to read-out contents."
                         "backing file too small? Expecting %" PRIu32" bytes",
                          prefix,
                          (unsigned int) (nr_bytes));
        }
    }

    s->timer_ps = ptimer_init(timer_ps_hit, s, PTIMER_POLICY_DEFAULT);
    s->timer_pgm = ptimer_init(timer_pgm_hit, s, PTIMER_POLICY_DEFAULT);

    /* Micro-seconds.  */
    ptimer_transaction_begin(s->timer_ps);
    ptimer_set_freq(s->timer_ps, 1000 * 1000);
    ptimer_transaction_commit(s->timer_ps);

    ptimer_transaction_begin(s->timer_pgm);
    ptimer_set_freq(s->timer_pgm, 1000 * 1000);
    ptimer_transaction_commit(s->timer_pgm);
}

static Property efuse_properties[] = {
    DEFINE_PROP_UINT8("efuse-nr", XLNXEFuse, efuse_nr, 3),
    DEFINE_PROP_UINT32("efuse-size", XLNXEFuse, efuse_size, 64 * 32),
    DEFINE_PROP_DRIVE("drive", XLNXEFuse, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_efuse = {
    .name = TYPE_XLNX_EFUSE,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(programming, XLNXEFuse),
        VMSTATE_PTIMER(timer_ps, XLNXEFuse),
        VMSTATE_PTIMER(timer_ps, XLNXEFuse),
        VMSTATE_END_OF_LIST(),
    }
};

static void efuse_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = efuse_realize;
    dc->vmsd = &vmstate_efuse;
    dc->props = efuse_properties;
}

static const TypeInfo efuse_info = {
    .name          = TYPE_XLNX_EFUSE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XLNXEFuse),
    .class_init    = efuse_class_init,
};

static void efuse_register_types(void)
{
    type_register_static(&efuse_info);
}
type_init(efuse_register_types)
