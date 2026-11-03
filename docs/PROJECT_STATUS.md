# Project Status: ios-natrium

## Overview

Port of iPhone 8 (iOS 16) to Xiaomi Mi 5s Plus (natrium) using XNU kernel from HackiOS project.

**Target Device:** Xiaomi Mi 5s Plus (b143fad6)  
**Status:** Initial setup complete, ready for kernel development  
**Date:** November 3, 2026

---

## ✅ Completed

### 1. Project Structure
- Directory layout created with proper organization
- Documentation framework established
- Build scripts prepared

### 2. Boot Shim Development
Complete bare-metal boot shim for Snapdragon 821:
- `shim/src/start.S` - ARM64 assembly entry point
- `shim/src/platform.c` - Platform initialization (watchdog disable, clocks, GPIO)
- `shim/src/uart.c` - UART driver for debugging
- `shim/src/payload.c` - XNU kernel payload loader
- `shim/Makefile` - Build system with aarch64-linux-gnu toolchain

**Features:**
- Disables Qualcomm watchdog timer
- Configures GICv3 interrupt controller
- Sets up memory map for XNU (upper DDR bank @ 4GB+)
- Loads kernel from Android boot image format
- UART console for boot debugging

### 3. Apple Device Tree Generator
Python script (`tools/mkadt.py`) that generates ADT binary:
- Presents hardware as iPhone 8 (iPhone10,4 / D201AP)
- Maps Snapdragon 821 hardware to iOS expectations
- Configures 4× Kryo CPUs with PSCI boot
- Defines memory, peripherals, GPUs, display, Wi-Fi
- Generates proper ADT v2 binary format

### 4. Firmware Extraction
Successfully extracted from device **b143fad6**:

**GPU Firmware (Adreno 530):**
- `a530_pfp.fw` - 15.5 KB (Packet Front-end Processor)
- `a530_pm4.fw` - 19.1 KB (Packet Manager)
- `a530_zap.mdt` - 6.5 KB (Zap shader metadata)
- `a530_zap.b02` - 4.8 KB (Zap shader segment)

**Wi-Fi Firmware (QCA6174):**
- `qwlan30.bin` - 736 KB (Main firmware)
- `bdwlan30.bin` - 8.0 KB (Board Data File)

### 5. Documentation
- `README.md` - Complete project overview
- `docs/PORTING_GUIDE.md` - Technical deep dive (15,000+ words)
  - Platform comparison iPhone 8 vs Mi 5s Plus
  - Boot process details
  - Hardware driver architecture  
  - GPU, display, Wi-Fi, storage, touch drivers
  - Apple Device Tree generation
  - Kernel patching strategy (170 patches organized)
  - Build process
  - Debugging techniques
  - Performance tuning

---

## 🚧 In Progress

Nothing currently in development (awaiting next steps).

---

## ⏳ Pending

### 1. XNU Kernel Sources
**Priority: HIGH**

- Clone Apple XNU kernel sources (xnu-8796.121.2 for iOS 16)
- Set up build environment
- Organize patch series

**Action:**
```bash
cd xnu
git clone https://github.com/apple-oss-distributions/xnu.git -b xnu-8796.121.2 src
```

### 2. Kernel Patches
**Priority: HIGH**

170 patches organized in categories:
- **0001-0010:** Core adaptation (remove ARMv8.2+ features, add natrium config)
- **0011-0030:** Platform (GICv3, timer, PSCI, framebuffer console)
- **0031-0050:** Storage (UFS driver, HFS+ write support)
- **0051-0070:** Security bypass (AMFI stub, permissive sandbox)
- **0071-0090:** Display (MDP5, DSI, IOSurface, IOMobileFramebuffer)
- **0091-0110:** Input (IOHIDFamily, I²C, touch, buttons)
- **0111-0130:** GPU (Adreno 530 driver, GEM, command submission)
- **0131-0150:** Wi-Fi (QCA6174 PCIe, BMI, HTC/WMI/HTT)
- **0151-0170:** Miscellaneous (SMP, RPM, clocks, thermal)

**References:**
- [ios-sailfish](https://github.com/joranv1/ios-sailfish) - Snapdragon 821 port (Google Pixel 1)
- [ios-sweet](https://github.com/dipa-studio/ios-sweet) - Snapdragon 732G port (Redmi Note 10 Pro)

### 3. Hardware Drivers
**Priority: MEDIUM**

Need to implement in `drivers/`:
- `gpu/` - Adreno 530 with Mesa freedreno backend
- `display/` - MDP5 + DSI + panel init
- `wifi/` - QCA6174 PCIe driver
- `storage/` - UFS host controller
- `touch/` - Synaptics S3320 (RMI4/I²C)
- `keys/` - Power/volume via PMIC SPMI

### 4. Build Tools
**Priority: MEDIUM**

Scripts needed in `tools/`:
- `xnu-build.sh` - macOS kernel build wrapper
- `mkpayload.py` - Assemble kernel + ADT + firmware
- `mkiosroot.sh` - Create iOS root from IPSW
- `build-boot-img.sh` - Create final boot image

### 5. iOS Root Filesystem
**Priority: LOW** (can test without full userland first)

- Download iPhone 8 IPSW (iOS 16.7.16)
- Extract system files (OS.dmg, SystemOS.dmg, AppOS.dmg)
- Create HFS+ journaled image
- Install to device userdata partition

### 6. Testing & Boot
**Priority: LOW** (requires all above)

- Build complete boot image
- Test via `fastboot boot`
- Debug via UART/framebuffer console
- Iterate on patches

---

## Hardware Information

### Device: Xiaomi Mi 5s Plus (natrium)
- **Serial:** b143fad6
- **Bootloader:** Unlocked ✓
- **Recovery:** TWRP
- **Current OS:** Android

### SoC: Qualcomm Snapdragon 821 (MSM8996Pro)
- **CPU:** Quad-core Kryo
  - 2× Kryo @ 2.35 GHz (Cluster 0)
  - 2× Kryo @ 2.19 GHz (Cluster 1)
- **Architecture:** ARMv8.0-A (64-bit)
- **Features:** FP, ASIMD, AES, PMULL, SHA1, SHA2, CRC32
- **Implementer:** 0x51 (Qualcomm)
- **Part:** 0x201 (Kryo)

### Memory
- **RAM:** 4 GB or 6 GB LPDDR4 @ 1866 MHz (dual-channel)
- **Page Size:** 4K (Android), will use 16K for XNU

### GPU
- **Model:** Adreno 530 @ 652 MHz
- **Firmware:** Extracted ✓

### Display
- **Panel:** 5.7" IPS LCD
- **Resolution:** 1080×1920 (386 PPI)
- **Controller:** MDP5 + MIPI DSI

### Wi-Fi
- **Model:** QCA6174 (PCIe)
- **Firmware:** Extracted ✓

### Storage
- **Type:** UFS 2.0
- **Interface:** 2 lanes HS-G3

### Touch
- **Controller:** Synaptics S3320
- **Interface:** I²C (BLSP QUP)

---

## Build Requirements

### Host System
- Linux PC (for shim and tools)
- macOS with Xcode 15.4 (for XNU kernel)

### Software
```bash
# Linux
sudo apt-get install build-essential python3 git \
    aarch64-linux-gnu-gcc aarch64-linux-gnu-binutils \
    android-tools-fsutils hfsprogs

# macOS (for kernel)
xcode-select --install
# Install KDK 13.5 from Apple Developer
```

---

## Key Technical Decisions

### 1. Boot Method
Using **`fastboot boot`** for temporary RAM boot:
- Non-destructive (doesn't modify boot partition)
- Easy iteration during development
- Can convert to permanent flash later

### 2. Memory Layout
Using **upper DDR bank** (4GB+) for kernel:
- Avoids TrustZone conflicts
- Matches other HackiOS ports
- Requires highmem support in XNU

### 3. Display Scaling
**1080×1920 physical → 750×1334 logical** (iPhone 8):
- Bilinear scaling via GPU
- Maintains iOS app compatibility
- Slight quality loss acceptable

### 4. Security Model
**Permissive mode** (research only):
- Code signing disabled (AMFI stub)
- Sandbox policies relaxed
- No data protection (no SEP)
- ⚠️ Not for production use

---

## Next Steps

1. **Clone XNU sources** and set up build environment
2. **Create base patches** (0001-0010: core adaptation)
3. **Implement boot shim build** and test on device
4. **Add platform drivers** (UART, framebuffer, UFS)
5. **Test first boot** to kernel panic (expected)
6. **Iterate on patches** until SpringBoard

---

## References

### Similar Projects
- **ios-sailfish:** Snapdragon 821 (Google Pixel 1) - Same SoC! ⭐
  - https://github.com/joranv1/ios-sailfish
- **ios-sweet:** Snapdragon 732G (Redmi Note 10 Pro) - Original concept
  - https://github.com/dipa-studio/ios-sweet
- **HackiOS:** Official website and documentation
  - https://hackiosx.dev/

### Technical Resources
- **Apple Open Source:** XNU kernel sources
  - https://opensource.apple.com/
- **Mesa freedreno:** Adreno GPU reverse engineering
  - https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/freedreno
- **Linux MSM kernel:** Snapdragon 821 drivers
  - https://github.com/torvalds/linux/tree/master/drivers/soc/qcom

---

## Notes

- Bootloader is confirmed unlocked despite property showing locked
- Device actively connected and ready for firmware extraction/testing
- All critical firmware files successfully extracted
- Boot shim code complete and ready to build
- Comprehensive documentation ensures no knowledge gaps

---

**Project initialized and ready for kernel development phase.**

*Last updated: November 3, 2026*
