/*
 * hyperC evaluation platform.
 *
 * Copyright (C) socware.net <socware.help@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"

#define MAX_CLK   320
#define MAX_CNT   0xFFFFF

#define DEF_CNT   0x1
#define DEF_PSR   0x2

#define GPTM_CNT  0x0
#define GPTM_PSR  0x4
#define GPTM_CTRL 0x8
#define GPTM_DATA 0xc

#define I_MODE        0
#define I_ENABLE    1

#define TYPE_HC_GPTIMER "hc.timer"
#define HC_GPTIMER(obj) \
    OBJECT_CHECK(gpt_t, (obj), TYPE_HC_GPTIMER)

typedef struct {
    SysBusDevice busdev;
    ptimer_state *tmr;
    uint32_t cnt;
    uint32_t psr;
    uint32_t ctrl;
    qemu_irq irq;
    MemoryRegion mem;
} gpt_t;

static void hc_timer_tick(void *opaque)
{
    gpt_t *t = (gpt_t *) opaque;
    if (t->ctrl & (1 << I_ENABLE))
        qemu_irq_pulse(t->irq);
    if (!(t->ctrl & (1 << I_MODE)))
        t->ctrl &= ~(1 << I_ENABLE);
}

static uint64_t hc_timer_read(void *opaque, hwaddr offset, unsigned sz)
{
    gpt_t *t = (gpt_t *) opaque;
    switch (offset) {
    case GPTM_CNT:
        return t->cnt;
    case GPTM_PSR:
        return t->psr;
    case GPTM_CTRL:
        return t->ctrl;
    case GPTM_DATA:
        return ptimer_get_count(t->tmr)
            ? (abs(ptimer_get_count(t->tmr) - t->cnt) +
               1) & 0xfffff : 0;
    default:
        hw_error("hc_timer_read: Bad offset %x\n", (int)offset);
        return 0;
    }
    return 0;
}

static void hc_timer_write(void *opaque, hwaddr offset, uint64_t value,
               unsigned sz)
{
    gpt_t *t = (gpt_t *) opaque;

    switch (offset) {
    case GPTM_CNT:
        t->cnt = (value & MAX_CNT);
        ptimer_set_limit(t->tmr, t->cnt, 0);
        break;
    case GPTM_PSR:
        ptimer_stop(t->tmr);
        t->psr = value;
        ptimer_set_freq(t->tmr, MAX_CLK / (1 << (value & 0xf)));
        break;
    case GPTM_CTRL:
        t->ctrl = value;
        ptimer_stop(t->tmr);
        if (value & (1 << I_ENABLE)) {
            if (t->ctrl & (1 << I_MODE))    //periodic
                ptimer_run(t->tmr, 0);
            else
                ptimer_run(t->tmr, 1);
        }
        break;
    case GPTM_DATA:
        break;
    default:
        hw_error("hc_timer_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps cpu_ops = {
    .read = hc_timer_read,
    .write = hc_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int hc_timer_init(SysBusDevice * dev)
{
    gpt_t *t = HC_GPTIMER(dev);

    sysbus_init_irq(dev, &t->irq);
    t->tmr =
        ptimer_init(qemu_bh_new(hc_timer_tick, t), PTIMER_POLICY_DEFAULT);
    t->psr = DEF_PSR;
    ptimer_set_freq(t->tmr, MAX_CLK / (1 << (t->psr & 0x7)));
    t->cnt = DEF_CNT;
    ptimer_set_limit(t->tmr, t->cnt, 0);

    memory_region_init_io(&t->mem, OBJECT(t), &cpu_ops, t, "hc_timer",
                  0x100);
    sysbus_init_mmio(dev, &t->mem);

    return 0;
}

static void hc_timer_class_init(ObjectClass * klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);
    sdc->init = hc_timer_init;
}

static const TypeInfo hc_timer_info = {
    .name = TYPE_HC_GPTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(gpt_t),
    .class_init = hc_timer_class_init,
};

static void hc_timer_register_types(void)
{
    type_register_static(&hc_timer_info);
}

type_init(hc_timer_register_types)
