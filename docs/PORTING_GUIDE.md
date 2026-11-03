# iOS to Xiaomi Mi 5s Plus Porting Guide

## Technical Deep Dive

### Platform Comparison

| Feature | iPhone 8 | Mi 5s Plus | Adaptation |
|---------|----------|------------|------------|
| SoC | Apple A11 Bionic | Snapdragon 821 MSM8996Pro | XNU kernel patches |
| CPU | 2×Monsoon + 4×Mistral | 2×Kryo @ 2.35 GHz + 2×Kryo @ 2.19 GHz | PSCI boot, GICv3 |
| Architecture | ARMv8.2-A (PAC, SHA3) | ARMv8.0-A | Remove PAC instructions |
| GPU | Apple GPU (3-core) | Adreno 530 @ 652 MHz | Custom driver + freedreno |
| Display | 4.7" 750×1334 326 PPI | 5.7" 1080×1920 386 PPI | Bilinear scaling |
| RAM | 2 GB LPDDR4X | 4/6 GB LPDDR4 | Memory map adjustment |
| Storage | NVMe | UFS 2.0 | Custom UFS driver |
| Page Size | 16K | 16K (Linux usually 4K) | Match iOS |

### Boot Process Details

#### 1. Stock Bootloader (ABL)
The Mi 5s Plus uses Qualcomm's Android Boot Loader (ABL) chain:
```
PBL (Primary Boot Loader, ROM)
  → SBL (Secondary Boot Loader)
    → ABOOT (Application Bootloader)
      → fastboot protocol
```

**Requirements:**
- Unlocked bootloader via `fastboot oem unlock` or `fastboot flashing unlock`
- No permanent modifications to boot/recovery partitions
- Uses `fastboot boot` for temporary RAM boot

#### 2. Boot Shim
Custom bare-metal code that:
```c
// Pseudo-code overview
void shim_entry(void) {
    // 1. Disable Qualcomm watchdog
    writel(0, WDOG_BASE + WDOG_ENABLE);
    
    // 2. Stop all secondary CPUs (will be restarted via PSCI)
    send_sgi_to_all_cpus(STOP_SGI);
    
    // 3. Configure GIC for XNU expectations
    gicv3_init();
    
    // 4. Set up memory map
    // iOS expects contiguous physical memory starting at base
    ddr_init();
    
    // 5. Load kernel payload from boot image
    // Located after shim in Android boot.img format
    void *kernel = load_section("kernel");
    void *adt = load_section("adt");
    void *bootargs = load_section("bootargs");
    
    // 6. Set up ARM64 boot protocol registers
    // x0 = dtb/adt pointer
    // x1 = 0
    // x2-x3 = reserved
    asm volatile(
        "mov x0, %0\n"
        "mov x1, xzr\n"
        "br %1\n"
        :: "r"(adt), "r"(kernel)
    );
}
```

**Memory Layout During Boot:**
```
0x00000000 - 0x7FFFFFFF  : Below 2GB (TrustZone, peripherals)
0x80000000 - 0xFFFFFFFF  : Lower RAM bank (1.5 GB usable)
0x100000000+             : Upper RAM bank (XNU loads here)
```

#### 3. XNU Kernel Initialization
Once XNU starts:
1. **Early boot**: Sets up MMU with 16K pages, 4-level tables
2. **Platform expert**: Reads Apple Device Tree, identifies as iPhone 8
3. **Device drivers**: Initializes UFS, framebuffer, interrupt controller
4. **Mount root**: `rd=ufsroot0` → UFS device 0 → HFS+ filesystem
5. **User space**: Exec `/sbin/launchd` with bootstrap

### Hardware Driver Architecture

#### GPU Driver (Adreno 530)
The Adreno 530 requires reverse-engineered Mesa freedreno knowledge:

**Kernel Components:**
- `bsd/dev/natrium_gpu.c`: Character device `/dev/natrium_gpu`
- `iokit/Drivers/arm/NatriumGPU.cpp`: IOKit service provider

**Memory Management:**
```c
// GEM-style buffer allocation
struct natrium_gpu_gem_new {
    uint64_t size;
    uint32_t flags;
    uint32_t handle;  // out
};

#define NATRIUM_GPU_GEM_NEW _IOWR('g', 0x01, struct natrium_gpu_gem_new)
```

**Command Submission:**
```c
struct natrium_gpu_submit {
    uint32_t gpu_id;
    uint32_t nr_bos;          // Number of buffers
    uint32_t nr_cmds;         // Number of command streams
    uint64_t bos;             // Pointer to handle array
    uint64_t cmds;            // Pointer to cmd structs
    uint32_t fence;           // out: fence for completion
};
```

**User Space Shim (`sweetgl.dylib`):**
Intercepts CoreAnimation/Metal calls:
```objc
// EAGL (OpenGL ES) → freedreno translation
- (BOOL)presentRenderbuffer:(NSUInteger)target {
    // 1. Flush GL commands to freedreno
    glFlush();
    
    // 2. Submit via natrium_gpu ioctl
    struct natrium_gpu_submit submit = {...};
    ioctl(gpu_fd, NATRIUM_GPU_SUBMIT, &submit);
    
    // 3. Copy result to IOSurface for compositor
    IOSurfaceLock(iosurface, 0, NULL);
    memcpy(IOSurfaceGetBaseAddress(iosurface), 
           gpu_framebuffer, width * height * 4);
    IOSurfaceUnlock(iosurface, 0, NULL);
}
```

#### Display Pipeline

**Hardware Chain:**
```
GPU Render
  → IOSurface (shared memory)
    → IOMobileFramebuffer (AppleCLCD driver)
      → MDP5 (Mobile Display Processor)
        → DSI Controller
          → JDI/Tianma Panel
```

**Panel Specifications:**
- Interface: MIPI DSI (4 lanes)
- Mode: Video mode with command support
- Resolution: 1080×1920 (portrait)
- Refresh: Up to 60 Hz
- Backlight: WLED via PMIC PWM

**Implementation Files:**
- `drivers/display/natrium_mdp.c`: MDP5 display controller
- `drivers/display/natrium_dsi.c`: DSI host controller
- `drivers/display/natrium_panel.c`: Panel initialization sequences
- `iokit/Drivers/arm/NatriumFramebuffer.cpp`: IOMobileFramebuffer

**Scaling Logic:**
```
iOS Logical: 750×1334 @ 2x (1500×2668 rendered)
              ↓ (scale 0.72)
Physical:    1080×1920 (native panel)
```

#### Wi-Fi Driver (QCA6174)

**Hardware Interface:**
- Connection: PCIe Gen2 x1
- Firmware: `qwlan30.bin`, `otp30.bin`, `bdwlan30.bin`
- Calibration: Board Data File (BDF) from device

**Driver Layers:**
```
User Space (CoreWLAN, MobileWiFi)
  ↓ (IOKit)
IOKit Driver (NatriumWLAN)
  ↓ (WMI/HTC/HTT protocols)
Kernel Driver (natrium_wlan.c)
  ↓ (PCIe BAR access)
QCA6174 Firmware
```

**Initialization Sequence:**
1. PCIe enumeration and BAR mapping
2. BMI (Boot loader Messaging Interface): Download firmware
3. HTC (Host Target Communication): Set up message pipes
4. WMI (Wireless Module Interface): Configure MAC, scan, connect
5. HTT (Host Target Transport): Data path for packets

**WPA2 Implementation:**
Since iOS expects different crypto APIs:
```c
// Custom 4-way handshake in kernel
int natrium_wlan_4way_handshake(struct natrium_wlan_dev *dev,
                                  const u8 *bssid, const u8 *psk) {
    // 1. Generate PTK from PMK (PSK-derived)
    // 2. Send EAPOL frames manually
    // 3. Install keys via WMI to firmware
    wmi_add_cipher_key(dev, WMI_CIPHER_AES_CCM, ptk, ...);
}
```

#### Storage Driver (UFS)

**UFS Host Controller:**
- Base Address: `0x624000` (from device tree)
- Version: UFS HC 2.0
- Lanes: 2 (HS-G3, 5.8 Gbps each)
- Max LUNs: 8 (typically LUN 0 for user data)

**Boot-time Root Mount:**
```
XNU boot-args: rd=ufsroot0 ufsrw=1
  → natrium_ufs.c: Detect UFS device
  → Register as IOBlockStorageDevice
  → HFS+ driver mounts /dev/disk0s1 as root
```

**Partition Layout:**
The `userdata` partition (formerly Android data) becomes HFS+:
```
/dev/sda (UFS LUN 0)
├── /dev/sda1  - boot (unused, kept for bootloader)
├── /dev/sda2  - system (unused)
├── /dev/sda3  - cache (unused)
└── /dev/sda4  - userdata → HFS+ journaled (iOS root)
```

**Critical Implementation Detail:**
UFS on Snapdragon 821 has write cache quirks:
```c
// Disable write cache to prevent data corruption
ufs_query_flag(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
               QUERY_FLAG_IDN_WB_EN, 0);
```

#### Touch Driver

**Hardware:**
- Controller: Synaptics S3320 (RMI4 protocol)
- Interface: I²C via BLSP QUP (BAR Low-Speed Peripheral)
- Address: 0x70 (7-bit)
- Interrupt: GPIO 125 (rising edge)
- Resolution: 1080×1920 (matches panel)

**RMI4 Protocol:**
```
Page Select Register (0xFF) → Select function page
Function Descriptors → Discover capabilities
  F01: Device Control
  F11/F12: 2D Touch Sensor
  F34: Flash Memory Management (firmware update)
  F54: Analog Testing
```

**iOS Integration:**
```c
// Register as IOHIDFamily device
IOHIDDevice {
    VendorID: 0x05AC (Apple)
    ProductID: 0x0001 (Generic Touch)
    PrimaryUsage: kHIDUsage_Digitizer_TouchScreen
}
```

**Coordinate Transformation:**
```c
// Raw touch coordinates → iOS points
// Native panel: 1080×1920, portrait
// iOS expects: 750×1334 logical (1500×2668 rendered)
// But physical is 1080×1920, so direct mapping with scaling

int ios_x = raw_x * 750 / 1080;
int ios_y = raw_y * 1334 / 1920;
```

### Apple Device Tree Generation

The ADT replaces Linux's device tree with Apple's format:

**Critical Properties:**
```
/ {
    compatible = "iPhone10,4", "AppleARM";
    model = "iPhone 8";
    
    chosen {
        bootargs = "debug=0xc cpus=4 rd=ufsroot0 ...";
        chip-id = <0x8960>;  // Snapdragon 821 ID
        board-id = <0x0C>;   // D201AP board
    };
    
    cpus {
        cpu0: cpu@0 { ... };  // Kryo cores
        cpu1: cpu@1 { ... };
        cpu2: cpu@100 { ... };
        cpu3: cpu@101 { ... };
    };
    
    memory {
        #address-cells = <2>;
        #size-cells = <2>;
        // Upper DDR bank for kernel
        reg = <0x00000001 0x00000000  // Start: 4GB
               0x00000000 0xF0000000>; // Size: 3.75GB
    };
    
    arm-io {
        compatible = "arm-io,t8960";  // Fake Apple SoC ID
        
        ufs0: ufs@624000 {
            compatible = "arm-io,ufs";
            reg = <0x624000 0x1000>;
            interrupts = <GIC_SPI 265 IRQ_TYPE_LEVEL_HIGH>;
        };
        
        gic: interrupt-controller@9000000 {
            compatible = "arm,gic-v3";
            reg = <0x09000000 0x10000>,  // Distributor
                  <0x09100000 0x100000>; // Redistributor
        };
        
        serial0: serial@75b0000 {
            compatible = "arm-io,uart";  // UART DM
            reg = <0x075b0000 0x1000>;
        };
        
        // More peripherals...
    };
    
    product {
        hw.model = "D201AP";
        hw.product = "iPhone10,4";
        hw.target = "iPhone 8";
        hw.region = "US";  // Or user's region
    };
}
```

**Generation Script (`tools/mkadt.py`):**
```python
#!/usr/bin/env python3
import struct
import sys

class ADTNode:
    def __init__(self, name):
        self.name = name
        self.properties = {}
        self.children = []
    
    def add_property(self, key, value):
        self.properties[key] = value
    
    def serialize(self):
        # Apple Device Tree binary format
        # Different from Linux FDT!
        ...
```

### Kernel Patching Strategy

**Patch Categories:**

**0001-0010: Core Adaptation**
- Remove ARMv8.2+ dependencies (PAC, BTI)
- Add NATRIUM machine config
- Disable corecrypto dependencies
- Add console early print support

**0011-0030: Platform**
- GICv3 interrupt controller (device interrupts, not FIQs)
- ARM generic timer (PPI 19, virtual)
- PSCI CPU power management (secondary CPUs)
- Device tree (ADT) platform expert
- Framebuffer console for debugging

**0031-0050: Storage**
- UFS host controller driver
- HFS+ write support patches
- Read-write root filesystem
- DMA above 4GB support (for upper DDR)

**0051-0070: Security Bypass**
- Minimal AMFI (code signing stub)
- Permissive sandbox policies
- Stub AppleKeyStore (no data protection)
- Disable entitlement checks
- Allow task_for_pid() for debugging

**0071-0090: Display**
- MDP5 display controller
- DSI host controller
- Panel initialization
- IOSurface implementation
- IOMobileFramebuffer (AppleCLCD)
- Software composition pipeline

**0091-0110: Input**
- IOHIDFamily backport (version 1915.140.3)
- I²C BLSP QUP driver
- Synaptics RMI4 touch protocol
- SPMI PMIC (power button, volume)
- GPIO/TLMM for interrupts

**0111-0130: GPU**
- Adreno 530 kernel driver
- GEM buffer management
- Command submission interface
- SMMU (System MMU) for GPU
- Firmware loading (PM4, PFP, zap shader)

**0131-0150: Wi-Fi**
- QCA6174 PCIe initialization
- BMI firmware download
- HTC/WMI/HTT protocol stack
- Crypto for WPA2 (CCMP)
- Apple80211 interface shim

**0151-0170: Miscellaneous**
- PSCI SMP (bring up secondary CPUs)
- RPM (Resource Power Manager) interface
- Clock and regulator drivers
- Thermal management (basic)
- Voltage/frequency scaling

### Build Process

#### Prerequisites on Linux Host:
```bash
sudo apt-get install build-essential python3 python3-pip git \
    aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils \
    device-tree-compiler mtd-utils android-tools-fsutils \
    hfsprogs libfuse-dev pkg-config
```

#### XNU Kernel Compilation:
**Important**: XNU requires macOS with Xcode to build!

**On macOS:**
```bash
# Install Xcode 15.4
xcode-select --install

# Install KDK (Kernel Development Kit) 13.5
# Download from Apple Developer

# Clone XNU
cd xnu
git clone https://github.com/apple-oss-distributions/xnu.git -b xnu-8796.121.2 src

# Apply patches
cd src
for patch in $(ls ../patches/*.patch | sort); do
    echo "Applying $patch"
    patch -p1 < "$patch" || exit 1
done

# Build (use wrapper script)
cd ..
../tools/xnu-build-mac.sh src out-natrium

# Result: out-natrium/kernel.development.natrium
```

**Alternative: GitHub Actions**
Use macOS runners for automated builds:
```yaml
# .github/workflows/build-kernel.yml
name: Build XNU Kernel
on: [push]
jobs:
  build:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Apply patches
        run: |
          cd xnu/src
          for p in ../patches/*.patch; do patch -p1 < "$p"; done
      - name: Build
        run: ./tools/xnu-build-mac.sh
      - uses: actions/upload-artifact@v3
        with:
          name: kernel
          path: xnu/out-natrium/kernel.development.natrium
```

### Debugging

#### Serial Console:
Mi 5s Plus UART is on testpoints (requires hardware mod):
- TX: TP901
- RX: TP902
- GND: Any ground pad
- Baud: 115200n8

**Enable in boot-args:**
```
serial=3 debug=0xc44 kextlog=0xffff
```

#### Persistent Log Buffer:
Allocate RAM ring buffer for kernel logs:
```c
// In shim: Reserve last 4MB of RAM
#define LOG_BUFFER_BASE  (RAM_TOP - 0x400000)
#define LOG_BUFFER_SIZE  0x400000

// In XNU patch: Write to fixed address
void natrium_log_putc(char c) {
    static uint32_t offset = 0;
    volatile char *log = (char *)LOG_BUFFER_BASE;
    log[offset++ % LOG_BUFFER_SIZE] = c;
}
```

**Read after panic:**
```bash
# Boot into recovery
fastboot boot recovery.img

# Dump log region
adb shell "dd if=/dev/mem bs=1M skip=4092 count=4 of=/tmp/xnu.log"
adb pull /tmp/xnu.log
strings xnu.log
```

#### Framebuffer Console:
Early framebuffer inherited from bootloader:
```c
// Get from ABL
struct fbcon {
    void *base;      // 0x9D400000 (common framebuffer address)
    uint32_t width;  // 1080
    uint32_t height; // 1920
    uint32_t stride; // bytes per line
    uint32_t bpp;    // 32 (ARGB8888)
};

// Simple text rendering
void fbcon_putc(struct fbcon *fb, char c) {
    static uint32_t x = 0, y = 0;
    // 8×16 bitmap font
    draw_char(fb, x, y, c);
    x += 8;
    if (x >= fb->width || c == '\n') { x = 0; y += 16; }
}
```

## Advanced Topics

### Power Management

iOS expects specific power states:
```c
// Minimal PSCI implementation
enum psci_state {
    PSCI_CPU_OFF = 0,
    PSCI_CPU_ON,
    PSCI_CPU_SUSPEND,
    PSCI_SYSTEM_OFF,
    PSCI_SYSTEM_RESET,
};

// SMC call to TrustZone
long psci_cpu_on(uint64_t mpidr, uint64_t entry) {
    register uint64_t x0 asm("x0") = 0xC4000003;  // PSCI_CPU_ON
    register uint64_t x1 asm("x1") = mpidr;
    register uint64_t x2 asm("x2") = entry;
    asm volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2));
    return x0;
}
```

### GPU Acceleration

**Metal Shading Language:**
iOS apps use Metal, not OpenGL ES. Shim layer translates:
```objc
// Metal → OpenGL ES 3.1 translation
@implementation MTLDeviceShim
- (id<MTLCommandQueue>)newCommandQueue {
    // Create GL context
    EGLContext ctx = eglCreateContext(...);
    return [[MTLCommandQueueGL alloc] initWithContext:ctx];
}
@end
```

**freedreno Integration:**
```c
// In sweetgl.dylib
void *metal_create_texture(size_t width, size_t height) {
    struct fd_resource *rsc;
    rsc = fd_resource_create(...);
    
    // Export as IOSurface for system compositor
    int dmabuf = fd_resource_get_dmabuf(rsc);
    IOSurfaceRef surf = IOSurfaceCreateFromDMABuf(dmabuf);
    return surf;
}
```

### Memory Management

**Zoned Allocator:**
XNU uses zone allocator, not slab:
```c
// Kernel memory zones
zone_t vm_object_zone;
zone_t vm_map_entry_zone;
zone_t ipc_port_zone;

// Allocation
void *obj = zalloc(vm_object_zone);
```

**Physical Memory:**
```c
// Mark TrustZone regions as reserved
vm_reserve_region(0x86000000, 0x00400000, "TZ Apps");
vm_reserve_region(0x86400000, 0x01C00000, "TZ Memory");

// Usable regions
vm_add_region(0x80000000, 0x06000000, "Lower DDR");
vm_add_region(0x100000000, 0xF0000000, "Upper DDR");
```

## Performance Tuning

### CPU Frequency Scaling:
```c
// Conservative governor for battery life
write_sysreg(CPUFREQ_CONSERVATIVE, cpufreq_policy);

// Or turbo mode for benchmarks
if (boot_arg_present("natriumturbo")) {
    set_cpu_freq(0, 1593600);  // Big cores: 1.59 GHz
    set_cpu_freq(2, 2150400);  // LITTLE: 2.15 GHz
}
```

### GPU Performance:
```c
// Max clocks for smooth 60fps
adreno_set_freq(652800000);  // 652.8 MHz
```

### Memory Bandwidth:
```c
// DDR frequency via RPM
rpm_set_ddr_freq(1866000000);  // 1866 MHz
```

## Troubleshooting

### Device won't boot
1. Check bootloader is unlocked: `fastboot oem device-info`
2. Verify shim loads: Add debug prints before kernel jump
3. Check kernel is ARM64, not x86_64
4. Ensure ADT is correct architecture

### Kernel panic on boot
1. Enable verbose boot: `debug=0xfff`
2. Check serial console output
3. Verify all patches applied correctly
4. Look for missing drivers in panic backtrace

### Display stays black
1. Check framebuffer address: `natriumfb_base=0x9D400000`
2. Verify panel power sequence
3. Test with simplest mode: `natriumfb_hz=30`
4. Check DSI lanes configuration

### Touch not working
1. Verify I²C bus active: Read Synaptics chip ID
2. Check GPIO interrupt routing
3. Ensure IOHIDFamily loads
4. Validate coordinate transformation

### Wi-Fi can't connect
1. Firmware files present in payload?
2. PCIe enumeration successful?
3. Check WPA2 passphrase encoding (UTF-8)
4. Try open network first

## Future Work

- [ ] Bluetooth support (QCA6174 BT over UART)
- [ ] Audio HAL (WCD9335 codec, Qualcomm ADSP)
- [ ] Camera pipeline (13MP Sony IMX258/IMX378)
- [ ] Sensors via SLPI (Snapdragon Sensor Core)
- [ ] Modem integration (MDM9x45 baseband)
- [ ] GPS (Qualcomm IZat location engine)
- [ ] Haptics (Qualcomm haptics driver)
- [ ] Battery management (PM8996 PMIC)
- [ ] Charging (SMB1351 charger IC)
- [ ] NFC (NXP PN548)
- [ ] Fingerprint (Goodix/FPC sensor)

## Conclusion

This is a comprehensive technical undertaking requiring:
- Deep ARM64 and XNU kernel knowledge
- Qualcomm SoC hardware documentation
- Reverse engineering skills
- Patience and persistence

The result is a functional iOS environment on Android hardware, demonstrating the portability of Apple's open-source XNU kernel.

**Remember:** This is for research and education only!
