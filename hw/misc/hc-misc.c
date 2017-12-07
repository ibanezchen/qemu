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
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/sysbus.h"

void hc_misc_init(Object* obj);

static uint64_t hc_misc_read(void *opaque, hwaddr offset, unsigned size)
{
    switch(offset){
    case 0x00:
        return 0x12345678;
    default:
        hw_error("hc_misc_read: Bad offset %x\n", (int)offset);
        return 0;
    }
    return 0;
}

static void hc_misc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    switch(offset){
    case 0x04:
        exit(value);
        break;
    default:
        hw_error("hc_misc_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps cpu_ops = {
    .read  = hc_misc_read,
    .write = hc_misc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define TYPE_HC_MISC	"hc.misc"
#define HC_MISC(obj)	\
	OBJECT_CHECK(hc_misc, (obj), TYPE_HC_MISC)

typedef struct hc_misc {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
}hc_misc;

void hc_misc_init(Object* obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    hc_misc* misc = HC_MISC(obj);
    memory_region_init_io(&misc->iomem, obj, &cpu_ops, (void*)0, TYPE_HC_MISC, 0x100);
    sysbus_init_mmio(sd, &misc->iomem);
}

static const TypeInfo hc_misc_info = {
    .name          = TYPE_HC_MISC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(hc_misc),
    .instance_init = hc_misc_init,
};

static void hc_misc_register_types(void)
{
    type_register_static(&hc_misc_info);
}

type_init(hc_misc_register_types)

