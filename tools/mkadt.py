#!/usr/bin/env python3
"""
Apple Device Tree Generator for Xiaomi Mi 5s Plus (natrium)
Generates ADT binary compatible with XNU kernel

Based on ios-sweet/ios-sailfish device tree generation
"""

import struct
import sys
import os
from typing import Dict, List, Any, Union

class ADTProperty:
    """Represents a property in Apple Device Tree"""
    def __init__(self, name: str, value: Union[bytes, str, int, List[int]]):
        self.name = name
        if isinstance(value, str):
            self.value = value.encode('utf-8') + b'\0'
        elif isinstance(value, int):
            self.value = struct.pack('>I', value)
        elif isinstance(value, list):
            self.value = b''.join(struct.pack('>I', v) for v in value)
        else:
            self.value = value

    def serialize(self) -> bytes:
        """Serialize property to binary format"""
        name_bytes = self.name.encode('utf-8') + b'\0'
        # Pad name to 4-byte alignment
        name_padded = name_bytes + b'\0' * ((4 - len(name_bytes) % 4) % 4)

        # Value length and data
        val_len = struct.pack('<I', len(self.value))
        val_padded = self.value + b'\0' * ((4 - len(self.value) % 4) % 4)

        return name_padded + val_len + val_padded

class ADTNode:
    """Represents a node in Apple Device Tree"""
    def __init__(self, name: str):
        self.name = name
        self.properties: List[ADTProperty] = []
        self.children: List[ADTNode] = []

    def add_property(self, name: str, value: Any) -> None:
        """Add a property to this node"""
        self.properties.append(ADTProperty(name, value))

    def add_child(self, child: 'ADTNode') -> 'ADTNode':
        """Add a child node"""
        self.children.append(child)
        return child

    def serialize(self) -> bytes:
        """Serialize node and all children to binary"""
        result = b''

        # Node header
        name_bytes = self.name.encode('utf-8') + b'\0'
        name_padded = name_bytes + b'\0' * ((4 - len(name_bytes) % 4) % 4)

        prop_count = struct.pack('<I', len(self.properties))
        child_count = struct.pack('<I', len(self.children))

        result += name_padded + prop_count + child_count

        # Serialize properties
        for prop in self.properties:
            result += prop.serialize()

        # Serialize children
        for child in self.children:
            result += child.serialize()

        return result

def create_natrium_adt(highmem: bool = True) -> ADTNode:
    """
    Create Apple Device Tree for Mi 5s Plus

    Args:
        highmem: Use upper DDR bank (4GB+) for kernel
    """
    root = ADTNode("device-tree")

    # Root properties
    root.add_property("compatible", "iPhone10,4\0AppleARM")
    root.add_property("model", "iPhone 8")
    root.add_property("#address-cells", [2])
    root.add_property("#size-cells", [2])

    # Chosen node (boot configuration)
    chosen = root.add_child(ADTNode("chosen"))
    bootargs = (
        "debug=0xc serial=3 cpus=4 keepsyms=1 "
        "rd=ufsroot0 ufsrw=1 rootrw=1 "
        "cs_enforcement_disable=1 amfi_get_out_of_my_way=1 "
        "wdt=-1 launchd_unsecure_cache=1 "
        "natriumfb_hz=60 natriumfb_base=0x9D400000"
    )
    chosen.add_property("boot-args", bootargs)
    chosen.add_property("chip-id", [0x8996])  # MSM8996Pro
    chosen.add_property("board-id", [0x0C])   # D201AP
    chosen.add_property("unique-chip-id", [0x1234567890ABCDEF])  # Fake ECID

    # Product identification
    product = root.add_child(ADTNode("product"))
    product.add_property("product-name", "iPhone10,4")
    product.add_property("product-description", "iPhone 8")
    product.add_property("product-soc-name", "T8960")  # Fake Apple SoC
    product.add_property("region-info", "US/A")
    product.add_property("device-color", "1")  # Space Gray
    product.add_property("cover-glass-color", "1")

    # CPU configuration
    cpus = root.add_child(ADTNode("cpus"))
    cpus.add_property("#address-cells", [1])
    cpus.add_property("#size-cells", [0])

    # Kryo cores: 2×2.35 GHz + 2×2.19 GHz
    # Clusters: 0-1 = LITTLE (2.35 GHz), 2-3 = big (2.19 GHz) in MSM8996Pro
    cpu_configs = [
        (0, 0x000, "cpu-efficiency", 2342400),  # Kryo L0
        (1, 0x001, "cpu-efficiency", 2342400),  # Kryo L1
        (2, 0x100, "cpu-performance", 2188800), # Kryo b0
        (3, 0x101, "cpu-performance", 2188800), # Kryo b1
    ]

    for cpu_id, mpidr, cpu_type, freq in cpu_configs:
        cpu = cpus.add_child(ADTNode(f"cpu{cpu_id}"))
        cpu.add_property("device_type", "cpu")
        cpu.add_property("compatible", f"{cpu_type}\0arm,armv8")
        cpu.add_property("reg", [mpidr])
        cpu.add_property("cpu-id", [cpu_id])
        cpu.add_property("cluster-id", [mpidr >> 8])
        cpu.add_property("state", "running" if cpu_id == 0 else "off")
        cpu.add_property("enable-method", "psci")
        cpu.add_property("cpu-release-addr", [0])
        cpu.add_property("clock-frequency", [freq * 1000])  # Hz
        cpu.add_property("timebase-frequency", [19200000])  # 19.2 MHz timer

    # Memory configuration
    memory = root.add_child(ADTNode("memory"))
    memory.add_property("device_type", "memory")

    if highmem:
        # Upper DDR bank: 4GB base, 3.75GB size (leaving room for framebuffer)
        memory.add_property("reg", [
            0x00000001, 0x00000000,  # Base: 0x1_00000000
            0x00000000, 0xF0000000   # Size: 3.75 GB
        ])
    else:
        # Lower DDR bank: 2GB base, 1.5GB size
        memory.add_property("reg", [
            0x00000000, 0x80000000,  # Base: 0x80000000
            0x00000000, 0x60000000   # Size: 1.5 GB
        ])

    # ARM I/O space (peripherals)
    arm_io = root.add_child(ADTNode("arm-io"))
    arm_io.add_property("compatible", "arm-io,t8960")
    arm_io.add_property("#address-cells", [1])
    arm_io.add_property("#size-cells", [1])
    arm_io.add_property("ranges", b'')  # Empty = identity mapping

    # GICv3 interrupt controller
    gic = arm_io.add_child(ADTNode("interrupt-controller"))
    gic.add_property("compatible", "arm,gic-v3")
    gic.add_property("interrupt-controller", b'')
    gic.add_property("#interrupt-cells", [3])
    gic.add_property("reg", [
        0x09BC0000, 0x10000,    # Distributor
        0x09C00000, 0x100000    # Redistributor
    ])
    gic.add_property("redistributor-stride", [0x20000])
    gic.add_property("#redistributor-regions", [1])

    # Serial console (UART DM)
    serial = arm_io.add_child(ADTNode("serial0"))
    serial.add_property("compatible", "qcom,msm-uartdm-v1.4")
    serial.add_property("reg", [0x075B0000, 0x1000])
    serial.add_property("interrupts", [0, 114, 4])  # GIC_SPI 114 IRQ_TYPE_LEVEL_HIGH
    serial.add_property("clock-frequency", [7372800])

    # UFS storage controller
    ufs = arm_io.add_child(ADTNode("ufs"))
    ufs.add_property("compatible", "qcom,ufshc\0jedec,ufs-2.0")
    ufs.add_property("reg", [0x00624000, 0x2500])
    ufs.add_property("interrupts", [0, 265, 4])  # GIC_SPI 265
    ufs.add_property("phys", [0x00627000])  # UFS PHY address
    ufs.add_property("freq-table-hz", [100000000, 200000000])  # Min, Max
    ufs.add_property("lanes-per-direction", [2])

    # Display subsystem
    mdss = arm_io.add_child(ADTNode("mdss"))
    mdss.add_property("compatible", "qcom,mdss")
    mdss.add_property("reg", [0x00900000, 0x90000])
    mdss.add_property("#address-cells", [1])
    mdss.add_property("#size-cells", [1])

    # MDP5 display controller
    mdp = mdss.add_child(ADTNode("mdp"))
    mdp.add_property("compatible", "qcom,mdp5")
    mdp.add_property("reg", [
        0x00901000, 0x90000,   # MDP base
        0x009B0000, 0x1040     # VBIF base
    ])
    mdp.add_property("interrupts", [0, 83, 4])  # MDP interrupt
    mdp.add_property("clocks", [0x901000])  # Simplified clock reference

    # DSI controller
    dsi = mdss.add_child(ADTNode("dsi0"))
    dsi.add_property("compatible", "qcom,mdss-dsi-ctrl")
    dsi.add_property("reg", [
        0x00994000, 0x400,     # DSI0 base
        0x00994400, 0x588,     # DSI0 PHY
    ])
    dsi.add_property("interrupts", [0, 82, 4])
    dsi.add_property("phys", [0x00994400])
    dsi.add_property("panel-width", [1080])
    dsi.add_property("panel-height", [1920])

    # Framebuffer (inherited from bootloader)
    framebuffer = arm_io.add_child(ADTNode("framebuffer"))
    framebuffer.add_property("compatible", "simple-framebuffer")
    framebuffer.add_property("reg", [0x9D400000, 0x800000])  # 8MB FB
    framebuffer.add_property("width", [1080])
    framebuffer.add_property("height", [1920])
    framebuffer.add_property("stride", [1080 * 4])
    framebuffer.add_property("format", "a8r8g8b8")

    # GPU (Adreno 530)
    gpu = arm_io.add_child(ADTNode("gpu"))
    gpu.add_property("compatible", "qcom,adreno-530.2\0qcom,adreno")
    gpu.add_property("reg", [
        0x05000000, 0x40000,   # GPU core
        0x0509A000, 0x10       # RBBM
    ])
    gpu.add_property("interrupts", [0, 300, 4])
    gpu.add_property("operating-points-v2", [
        652800000,             # Max frequency: 652.8 MHz
        600000000,             # Normal
        510000000,
        401800000,
        315000000,
        214000000,
        133000000
    ])

    # I2C BLSP QUP (for touch)
    i2c7 = arm_io.add_child(ADTNode("i2c7"))
    i2c7.add_property("compatible", "qcom,i2c-qup-v2.2.1")
    i2c7.add_property("reg", [0x75B5000, 0x600])
    i2c7.add_property("interrupts", [0, 101, 4])  # BLSP2_QUP1_IRQ
    i2c7.add_property("#address-cells", [1])
    i2c7.add_property("#size-cells", [0])
    i2c7.add_property("clock-frequency", [400000])  # 400 KHz

    # Touchscreen on I2C7
    touch = i2c7.add_child(ADTNode("synaptics"))
    touch.add_property("compatible", "syna,rmi4-i2c")
    touch.add_property("reg", [0x70])  # I2C address
    touch.add_property("interrupt-parent", [0])  # GPIO controller
    touch.add_property("interrupts", [125, 0x2008])  # GPIO 125, falling edge
    touch.add_property("syna,reset-delay-ms", [200])
    touch.add_property("panel", [1080, 1920])

    # PCIe (for Wi-Fi)
    pcie0 = arm_io.add_child(ADTNode("pcie0"))
    pcie0.add_property("compatible", "qcom,pcie-msm8996")
    pcie0.add_property("reg", [
        0x00600000, 0x2000,    # DBI
        0x0C000000, 0xF1D,     # ELBI
        0x0C000F20, 0xA8,      # PARF
        0x0C100000, 0x100000   # Config space
    ])
    pcie0.add_property("linux,pci-domain", [0])
    pcie0.add_property("bus-range", [0, 0xFF])
    pcie0.add_property("#address-cells", [3])
    pcie0.add_property("#size-cells", [2])
    pcie0.add_property("device_type", "pci")

    # PMIC (Power Management IC)
    spmi = arm_io.add_child(ADTNode("spmi"))
    spmi.add_property("compatible", "qcom,spmi-pmic-arb")
    spmi.add_property("reg", [0x04044000, 0x1100])
    spmi.add_property("#address-cells", [2])
    spmi.add_property("#size-cells", [0])

    # PM8996 PMIC
    pmic0 = spmi.add_child(ADTNode("pmic0"))
    pmic0.add_property("compatible", "qcom,pm8996")
    pmic0.add_property("reg", [0, 0])  # SPMI USID 0

    # Power button
    pon = pmic0.add_child(ADTNode("pon"))
    pon.add_property("compatible", "qcom,pm8941-pon")
    pon.add_property("reg", [0x800])
    pon.add_property("pwrkey", b'')
    pon.add_property("linux,code", [116])  # KEY_POWER

    # Volume keys via GPIO
    gpio_keys = arm_io.add_child(ADTNode("gpio-keys"))
    gpio_keys.add_property("compatible", "gpio-keys")
    gpio_keys.add_property("#address-cells", [1])
    gpio_keys.add_property("#size-cells", [0])

    vol_up = gpio_keys.add_child(ADTNode("volume-up"))
    vol_up.add_property("label", "Volume Up")
    vol_up.add_property("linux,code", [115])  # KEY_VOLUMEUP
    vol_up.add_property("gpios", [2, 0])  # GPIO 2
    vol_up.add_property("wakeup-source", b'')

    vol_down = gpio_keys.add_child(ADTNode("volume-down"))
    vol_down.add_property("label", "Volume Down")
    vol_down.add_property("linux,code", [114])  # KEY_VOLUMEDOWN
    vol_down.add_property("gpios", [0, 0])  # GPIO 0
    vol_down.add_property("wakeup-source", b'')

    return root

def write_adt_binary(root: ADTNode, output_path: str) -> None:
    """Write ADT to binary file"""
    # Serialize tree
    data = root.serialize()

    # ADT header
    # Magic: 'aDT2' (Apple Device Tree v2)
    header = struct.pack('<4s', b'aDT2')
    header += struct.pack('<I', len(data) + 16)  # Total size
    header += struct.pack('<I', len(data))       # Data size
    header += struct.pack('<I', 0)               # Reserved

    # Write file
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(data)

    print(f"✓ Generated ADT: {output_path} ({len(data) + 16} bytes)")

def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='Generate Apple Device Tree for Xiaomi Mi 5s Plus'
    )
    parser.add_argument('output', help='Output ADT file path')
    parser.add_argument('--natrium', action='store_true',
                       help='Generate for Mi 5s Plus (default)')
    parser.add_argument('--highmem', action='store_true', default=True,
                       help='Use upper DDR bank (4GB+) for kernel')
    parser.add_argument('--lowmem', dest='highmem', action='store_false',
                       help='Use lower DDR bank (<4GB)')

    args = parser.parse_args()

    print("Generating Apple Device Tree for Xiaomi Mi 5s Plus...")
    print(f"  Memory mode: {'upper DDR (4GB+)' if args.highmem else 'lower DDR (<4GB)'}")

    adt = create_natrium_adt(highmem=args.highmem)
    write_adt_binary(adt, args.output)

    print("\nDevice configuration:")
    print("  Device: Xiaomi Mi 5s Plus (natrium)")
    print("  SoC: Snapdragon 821 MSM8996Pro")
    print("  Presents as: iPhone 8 (iPhone10,4 / D201AP)")
    print("  Display: 1080×1920 → 750×1334 logical")
    print("  CPUs: 4× Kryo (PSCI boot)")
    print("  Memory: 4/6 GB LPDDR4")
    print("\nReady for kernel payload assembly.")

if __name__ == '__main__':
    main()
