/*
 * QEMU model of ZynqMP APU Core Functionality
 *
 * For the most part, a dummy device model.
 *
 * Copyright (c) 2013-2020 Peter Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Partially autogenerated by xregqemu.py 2020-02-06.
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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"
#include "hw/fdt_generic_util.h"

#define TYPE_ZYNQMP_APU "xlnx.apu"

#define ZYNQMP_APU(obj) \
     OBJECT_CHECK(ZynqMPAPU, (obj), TYPE_ZYNQMP_APU)

#ifndef XILINX_ZYNQMP_APU_ERR_DEBUG
#define XILINX_ZYNQMP_APU_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (XILINX_ZYNQMP_APU_ERR_DEBUG >= lvl) {\
        qemu_log(TYPE_ZYNQMP_APU ": %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

REG32(ERR_CTRL, 0x0)
    FIELD(ERR_CTRL, PSLVERR, 0, 1)
REG32(ISR, 0x10)
    FIELD(ISR, INV_APB, 0, 1)
REG32(IMR, 0x14)
    FIELD(IMR, INV_APB, 0, 1)
REG32(IEN, 0x18)
    FIELD(IEN, INV_APB, 0, 1)
REG32(IDS, 0x1c)
    FIELD(IDS, INV_APB, 0, 1)
REG32(CONFIG_0, 0x20)
    FIELD(CONFIG_0, CFGTE, 24, 4)
    FIELD(CONFIG_0, CFGEND, 16, 4)
    FIELD(CONFIG_0, VINITHI, 8, 4)
    FIELD(CONFIG_0, AA64NAA32, 0, 4)
REG32(CONFIG_1, 0x24)
    FIELD(CONFIG_1, L2RSTDISABLE, 29, 1)
    FIELD(CONFIG_1, L1RSTDISABLE, 28, 1)
    FIELD(CONFIG_1, CP15DISABLE, 0, 4)
REG32(RVBARADDR0L, 0x40)
    FIELD(RVBARADDR0L, ADDR, 2, 30)
REG32(RVBARADDR0H, 0x44)
    FIELD(RVBARADDR0H, ADDR, 0, 8)
REG32(RVBARADDR1L, 0x48)
    FIELD(RVBARADDR1L, ADDR, 2, 30)
REG32(RVBARADDR1H, 0x4c)
    FIELD(RVBARADDR1H, ADDR, 0, 8)
REG32(RVBARADDR2L, 0x50)
    FIELD(RVBARADDR2L, ADDR, 2, 30)
REG32(RVBARADDR2H, 0x54)
    FIELD(RVBARADDR2H, ADDR, 0, 8)
REG32(RVBARADDR3L, 0x58)
    FIELD(RVBARADDR3L, ADDR, 2, 30)
REG32(RVBARADDR3H, 0x5c)
    FIELD(RVBARADDR3H, ADDR, 0, 8)
REG32(ACE_CTRL, 0x60)
    FIELD(ACE_CTRL, AWQOS, 16, 4)
    FIELD(ACE_CTRL, ARQOS, 0, 4)
REG32(SNOOP_CTRL, 0x80)
    FIELD(SNOOP_CTRL, ACE_INACT, 4, 1)
    FIELD(SNOOP_CTRL, ACP_INACT, 0, 1)
REG32(PWRCTL, 0x90)
    FIELD(PWRCTL, CLREXMONREQ, 17, 1)
    FIELD(PWRCTL, L2FLUSHREQ, 16, 1)
    FIELD(PWRCTL, CPUPWRDWNREQ, 0, 4)
REG32(PWRSTAT, 0x94)
    FIELD(PWRSTAT, CLREXMONACK, 17, 1)
    FIELD(PWRSTAT, L2FLUSHDONE, 16, 1)
    FIELD(PWRSTAT, DBGNOPWRDWN, 0, 4)

#define R_MAX ((R_PWRSTAT) + 1)

#define NUM_CPUS 4

typedef struct ZynqMPAPU ZynqMPAPU;

struct ZynqMPAPU {
    SysBusDevice busdev;
    MemoryRegion iomem;

    ARMCPU *cpus[NUM_CPUS];
    /* WFIs towards PMU. */
    qemu_irq wfi_out[4];
    /* CPU Power status towards INTC Redirect. */
    qemu_irq cpu_power_status[4];
    qemu_irq irq_imr;

    uint8_t cpu_pwrdwn_req;
    uint8_t cpu_in_wfi;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

static void update_wfi_out(void *opaque)
{
    ZynqMPAPU *s = ZYNQMP_APU(opaque);
    unsigned int i, wfi_pending;

    wfi_pending = s->cpu_pwrdwn_req & s->cpu_in_wfi;
    for (i = 0; i < NUM_CPUS; i++) {
        qemu_set_irq(s->wfi_out[i], !!(wfi_pending & (1 << i)));
    }
}

static void zynqmp_apu_reset(DeviceState *dev)
{
    ZynqMPAPU *s = ZYNQMP_APU(dev);
    int i;
 
    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }

    s->cpu_pwrdwn_req = 0;
    s->cpu_in_wfi = 0;
    update_wfi_out(s);
}

static void zynqmp_apu_rvbar_post_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPAPU *s = ZYNQMP_APU(reg->opaque);
    int i;

    for (i = 0; i < NUM_CPUS; ++i) {
        uint64_t rvbar = s->regs[R_RVBARADDR0L + 2 * i] +
                         ((uint64_t)s->regs[R_RVBARADDR0H + 2 * i] << 32);
        if (s->cpus[i]) {
            object_property_set_int(OBJECT(s->cpus[i]), "rvbar", rvbar, &error_abort);
            DB_PRINT("Set RVBAR %d to %" PRIx64 "\n", i, rvbar);
        }
    }
}

static void zynqmp_apu_pwrctl_post_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPAPU *s = ZYNQMP_APU(reg->opaque);
    unsigned int i, new;

    /*
     * !!HACK ALERT!!
     *
     * When ZynqMP ATF writes this register to power down itself,
     * the action implies the APU is not in WFI.
     *
     * However, it is still unknown why this APU's WFI-out is
     * sometimes still active at the time of this register written,
     * and that greatly confuses ZynqMP PMU firmware.
     *
     * The following is just a temporary hack to detect and correct
     * the wrong WFI-out state, until the root-cause is found and fixed.
     */
    CPUState *cs = current_cpu;
    if (cs) {
        uint64_t cpu_mask = 1 << cs->cpu_index;
        uint64_t self_suspend = cpu_mask & val;

        if ((self_suspend & s->cpu_in_wfi) != 0) {
            ARMCPU *apu = ARM_CPU(cs);
            bool is_atf = arm_current_el(&apu->env) > 1;

            if (is_atf) {
                apu->is_in_wfi = false;
                qemu_set_irq(apu->wfi, 0);
                assert((self_suspend & s->cpu_in_wfi) == 0);
            }
        }
    }
    /* End of Hack */

    for (i = 0; i < NUM_CPUS; i++) {
        new = val & (1 << i);
        /* Check if CPU's CPUPWRDNREQ has changed. If yes, update GPIOs. */
        if (new != (s->cpu_pwrdwn_req & (1 << i))) {
            qemu_set_irq(s->cpu_power_status[i], !!new);
        }
        s->cpu_pwrdwn_req &= ~(1 << i);
        s->cpu_pwrdwn_req |= new;
    }
    update_wfi_out(s);
}

static void imr_update_irq(ZynqMPAPU *s)
{
    bool pending = s->regs[R_ISR] & ~s->regs[R_IMR];
    qemu_set_irq(s->irq_imr, pending);
}

static void isr_postw(RegisterInfo *reg, uint64_t val64)
{
    ZynqMPAPU *s = ZYNQMP_APU(reg->opaque);
    imr_update_irq(s);
}

static uint64_t ien_prew(RegisterInfo *reg, uint64_t val64)
{
    ZynqMPAPU *s = ZYNQMP_APU(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] &= ~val;
    imr_update_irq(s);
    return 0;
}

static uint64_t ids_prew(RegisterInfo *reg, uint64_t val64)
{
    ZynqMPAPU *s = ZYNQMP_APU(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] |= val;
    imr_update_irq(s);
    return 0;
}

static const RegisterAccessInfo zynqmp_apu_regs_info[] = {
#define RVBAR_REGDEF(n) \
    {   .name = "RVBAR CPU " #n " Low",  .addr = A_RVBARADDR ## n ## L, \
            .reset = 0xffff0000ul,                                             \
            .post_write = zynqmp_apu_rvbar_post_write,                        \
    },{ .name = "RVBAR CPU " #n " High", .addr = A_RVBARADDR ## n ## H, \
            .post_write = zynqmp_apu_rvbar_post_write,                        \
    }
    {   .name = "ERR_CTRL",  .addr = A_ERR_CTRL,
    },{ .name = "ISR",  .addr = A_ISR,
        .w1c = 0x1,
        .post_write = isr_postw,
    },{ .name = "IMR",  .addr = A_IMR,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IEN",  .addr = A_IEN,
        .pre_write = ien_prew,
    },{ .name = "IDS",  .addr = A_IDS,
        .pre_write = ids_prew,
    },{ .name = "CONFIG_0",  .addr = A_CONFIG_0,
        .reset = 0xf0f,
    },{ .name = "CONFIG_1",  .addr = A_CONFIG_1,
    },
    RVBAR_REGDEF(0),
    RVBAR_REGDEF(1),
    RVBAR_REGDEF(2),
    RVBAR_REGDEF(3), { .name = "ACE_CTRL",  .addr = A_ACE_CTRL,
        .reset = 0xf000f,
    },{ .name = "SNOOP_CTRL",  .addr = A_SNOOP_CTRL,
    },{ .name = "PWRCTL",  .addr = A_PWRCTL,
        .post_write = zynqmp_apu_pwrctl_post_write,
    },{ .name = "PWRSTAT",  .addr = A_PWRSTAT,
        .ro = 0x3000f,
    }
};

static const MemoryRegionOps zynqmp_apu_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void zynqmp_apu_handle_wfi(void *opaque, int irq, int level)
{
    ZynqMPAPU *s = ZYNQMP_APU(opaque);

    s->cpu_in_wfi = deposit32(s->cpu_in_wfi, irq, 1, level);
    update_wfi_out(s);
}

static void zynqmp_apu_realize(DeviceState *dev, Error **errp)
{
    /* Delete this if not necessary */
}

static void zynqmp_apu_init(Object *obj)
{
    ZynqMPAPU *s = ZYNQMP_APU(obj);
    int i;
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, "MMIO", R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), zynqmp_apu_regs_info,
                              ARRAY_SIZE(zynqmp_apu_regs_info),
                              s->regs_info, s->regs,
                              &zynqmp_apu_ops,
                              XILINX_ZYNQMP_APU_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_imr);

    for (i = 0; i < NUM_CPUS; ++i) {
        char *prop_name = g_strdup_printf("cpu%d", i);
        object_property_add_link(obj, prop_name, TYPE_ARM_CPU,
                             (Object **)&s->cpus[i],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
        g_free(prop_name);
    }

    /* wfi_out is used to connect to PMU GPIs. */
    qdev_init_gpio_out_named(DEVICE(obj), s->wfi_out, "wfi_out", 4);
    /* CPU_POWER_STATUS is used to connect to INTC redirect. */
    qdev_init_gpio_out_named(DEVICE(obj), s->cpu_power_status,
                             "CPU_POWER_STATUS", 4);
    /* wfi_in is used as input from CPUs as wfi request. */
    qdev_init_gpio_in_named(DEVICE(obj), zynqmp_apu_handle_wfi, "wfi_in", 4);
}

static const VMStateDescription vmstate_zynqmp_apu = {
    .name = "zynqmp_apu",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqMPAPU, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet zynqmp_apu_controller_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[])  {
            { .name = "wfi_in", .fdt_index = 0, .range = 4 },
            { .name = "CPU_POWER_STATUS", .fdt_index = 4, .range = 4 },
            { },
        },
    },
    { },
};

static const FDTGenericGPIOSet zynqmp_apu_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[])  {
            { .name = "wfi_out",          .fdt_index = 0, .range = 4 },
            { },
        },
    },
    { },
};

static void zynqmp_apu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->reset = zynqmp_apu_reset;
    dc->realize = zynqmp_apu_realize;
    dc->vmsd = &vmstate_zynqmp_apu;
    fggc->controller_gpios = zynqmp_apu_controller_gpios;
    fggc->client_gpios = zynqmp_apu_client_gpios;
}

static const TypeInfo zynqmp_apu_info = {
    .name          = TYPE_ZYNQMP_APU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPAPU),
    .class_init    = zynqmp_apu_class_init,
    .instance_init = zynqmp_apu_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void zynqmp_apu_register_types(void)
{
    type_register_static(&zynqmp_apu_info);
}

type_init(zynqmp_apu_register_types)
