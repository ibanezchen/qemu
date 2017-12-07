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
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/cpu/a9mpcore.h"

static struct arm_boot_info hc_binfo;

typedef struct {
    MachineClass parent;
} HCMachineClass;

typedef struct {
    MachineState parent;
    bool secure;
} HCMachineState;

#define TYPE_HC_MACHINE     "hc"
#define TYPE_HC_A_MACHINE   MACHINE_TYPE_NAME("hc-a")
#define TYPE_HC_R_MACHINE   MACHINE_TYPE_NAME("hc-r")

#define HC_MACHINE(obj) \
    OBJECT_CHECK(HCMachineState, (obj), TYPE_HC_MACHINE)

#define HC_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(HCMachineClass, klass, TYPE_HC_MACHINE)

MemoryRegion *get_system_memory(void);

static DeviceState *
ethite_create(NICInfo *nd, hwaddr base, qemu_irq irq,
                      int txpingpong, int rxpingpong)
{
    DeviceState *dev;

    qemu_check_nic_model(nd, "hc.eth");

    dev = qdev_create(NULL, "hc.eth");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "tx-ping-pong", txpingpong);
    qdev_prop_set_uint32(dev, "rx-ping-pong", rxpingpong);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}

static void init_cpus(const char *cpu_type, const char *privdev,
                      hwaddr periphbase, qemu_irq *pic, bool secure)
{
    DeviceState *dev;
    SysBusDevice *busdev;
    int n;

    /* Create the actual CPUs */
    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj = object_new(cpu_type);

        if (!secure) {
            object_property_set_bool(cpuobj, false, "has_el3", NULL);
        }
        
        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, periphbase,
                                    "reset-cbar", &error_abort);
        }
        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
    }

    /* Create the private peripheral devices (including the GIC);
     * this must happen after the CPUs are created because a15mpcore_priv
     * wires itself up to the CPU's generic_timer gpio out lines.
     */
    dev = qdev_create(NULL, privdev);
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, periphbase);

    /* Interrupts [42:0] are from the motherboard;
     * [47:43] are reserved; [63:48] are daughterboard
     * peripherals. Note that some documentation numbers
     * external interrupts starting from 32 (because there
     * are internal interrupts 0..31).
     */
    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    /* Connect the CPUs to the GIC */
    for (n = 0; n < smp_cpus; n++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(n));

        sysbus_connect_irq(busdev, n, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(busdev, n + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
    }
}

static void hc_common_init(MachineState *machine)
{
    MemoryRegion *sysmem;
    unsigned base, size;
    qemu_irq pic[64];
    MemoryRegion *mem = g_new(MemoryRegion, 1);
    HCMachineState *hms = HC_MACHINE(machine);
    
    sysmem = get_system_memory();

    init_cpus(machine->cpu_type, TYPE_A9MPCORE_PRIV, 0xB0400000, pic, hms->secure);
    base = 0;
    size = 0x10000000;
    
    memory_region_allocate_system_memory(mem, NULL, "hc_dram", size);
    memory_region_add_subregion(sysmem, base, mem);

    serial_mm_init(sysmem, 0xB0200000, 2, pic[7], 115200, serial_hds[0], DEVICE_NATIVE_ENDIAN);
    if(serial_hds[1]) {
        serial_mm_init(sysmem, 0xB0300000, 2, pic[8], 115200, serial_hds[1], DEVICE_NATIVE_ENDIAN);
    }
    sysbus_create_simple("hc.timer", 0xB0500000, pic[0]);
    sysbus_create_simple("hc.timer", 0xB0600000, pic[1]);
    sysbus_create_simple("hc.rtc", 0xB0800000, pic[3]);
    sysbus_create_simple("hc.misc", 0xB0100000, NULL);

    ethite_create(&nd_table[0], 0xB0700000, pic[5], 0, 0);
    
    hc_binfo.ram_size        = size;
    hc_binfo.kernel_filename = machine->kernel_filename;
    hc_binfo.kernel_cmdline  = machine->kernel_cmdline;
    hc_binfo.initrd_filename = machine->initrd_filename;
    hc_binfo.board_id        = 0x3088;
    hc_binfo.loader_start    = 0x0;
    arm_load_kernel(ARM_CPU(first_cpu), &hc_binfo);

}

static bool hc_get_secure(Object *obj, Error **errp)
{
    HCMachineState *vms = HC_MACHINE(obj);
    return vms->secure;
}

static void hc_set_secure(Object *obj, bool value, Error **errp)
{
    HCMachineState *vms = HC_MACHINE(obj);
    vms->secure = value;
}

static void hc_instance_init(Object *obj)
{
    HCMachineState *vms = HC_MACHINE(obj);
    vms->secure = true;
    object_property_add_bool(obj, "secure", hc_get_secure,
                             hc_set_secure, NULL);
    object_property_set_description(obj, "secure",
                                    "Set on/off to enable/disable the ARM "
                                    "Security Extensions (TrustZone)",
                                    NULL);
}

static void hc_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "socware.net emulation platform";
    mc->init = hc_common_init;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
}

static void hc_a_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "socware.net cortex-A emulation";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
}

static void hc_r_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "socware.net cortex-R emulation";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-r5");
}

static const TypeInfo hc_info = {
    .name = TYPE_HC_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(HCMachineState),
    .instance_init = hc_instance_init,
    .class_size = sizeof(HCMachineClass),
    .class_init = hc_class_init,
};

static const TypeInfo hc_a_info = {
    .name = TYPE_HC_A_MACHINE,
    .parent = TYPE_HC_MACHINE,
    .class_init = hc_a_class_init,
};

static const TypeInfo hc_r_info = {
    .name = TYPE_HC_R_MACHINE,
    .parent = TYPE_HC_MACHINE,
    .class_init = hc_r_class_init,
};

static void hc_machine_init(void)
{
    type_register_static(&hc_info);
    type_register_static(&hc_a_info);
    type_register_static(&hc_r_info);
}

type_init(hc_machine_init);
