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


#define MAX_CLK 320

#define GPTM_CNT 0x0
#define GPTM_RTC 0x4

#define I_ENABLE 1

#define TYPE_HC_RTC "hc.rtc"
#define HC_RTC(obj) OBJECT_CHECK(rtc_t, (obj), TYPE_HC_RTC)

typedef struct {
    SysBusDevice busdev;
    ptimer_state* rtmr;
    uint32_t cnt;
    uint32_t rtc;
    qemu_irq irq;
    MemoryRegion mem;
} rtc_t;

static void hc_rtc_tick(void* opaque) {
    rtc_t* t = (rtc_t*)opaque;
    t->rtc++;
    if (t->cnt > 0) {
        if ((--t->cnt) == 0) qemu_irq_pulse(t->irq);
    }
}

static uint64_t hc_rtc_read(void* opaque, hwaddr offset, unsigned sz) {
    rtc_t* t = (rtc_t*)opaque;
    switch (offset) {
        case GPTM_CNT:
            return t->cnt;
        case GPTM_RTC:
            return ((uint64_t)t->rtc);
        default:
            hw_error("hc_rtc_read: Bad offset %x\n", (int)offset);
            return 0;
    }
    return 0;
}

static void hc_rtc_write(void* opaque, hwaddr offset, uint64_t value, unsigned sz) {
    rtc_t* t = (rtc_t*)opaque;

    switch (offset) {
        case GPTM_CNT:
            t->cnt = value;
            break;
        case GPTM_RTC:
            break;
        default:
            hw_error("hc_rtc_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps cpu_ops = {
    .read = hc_rtc_read,
    .write = hc_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int hc_rtc_init(SysBusDevice* dev) {
    rtc_t* t = HC_RTC(dev);

    sysbus_init_irq(dev, &t->irq);

    t->rtc = 10;
    t->rtmr = ptimer_init(qemu_bh_new(hc_rtc_tick, t), PTIMER_POLICY_DEFAULT);
    ptimer_set_freq(t->rtmr, MAX_CLK);
    ptimer_set_limit(t->rtmr, 0x1, 0);
    ptimer_run(t->rtmr, 0);

    memory_region_init_io(&t->mem, OBJECT(t), &cpu_ops, t, "hc_rtc", 0x100);
    sysbus_init_mmio(dev, &t->mem);

    return 0;
}

static void hc_rtc_class_init(ObjectClass* klass, void* data) {
    SysBusDeviceClass* sdc = SYS_BUS_DEVICE_CLASS(klass);
    sdc->init = hc_rtc_init;
}

static const TypeInfo hc_rtc_info = {
    .name = TYPE_HC_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(rtc_t),
    .class_init = hc_rtc_class_init,
};

static void hc_rtc_register_types(void) {
    type_register_static(&hc_rtc_info);
}

type_init(hc_rtc_register_types)
