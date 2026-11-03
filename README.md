# ios-natrium: iOS 16 on Xiaomi Mi 5s Plus

This project ports Apple's open-source XNU kernel (xnu-8796, iOS 16 generation) to the Xiaomi Mi 5s Plus (natrium, Snapdragon 821 MSM8996Pro) and runs the unmodified iOS 16.7.16 userland from iPhone 8.

## Overview

**Hardware Platform:**
- Device: Xiaomi Mi 5s Plus (codename: natrium)
- SoC: Qualcomm Snapdragon 821 MSM8996Pro
- CPU: Quad-core Kryo (2×2.35 GHz + 2×2.19 GHz)
- GPU: Adreno 530 (652 MHz)
- RAM: 4/6 GB LPDDR4 (1866 MHz dual-channel)
- Display: 5.7" IPS LCD, 1080×1920 pixels (386 PPI)

**iOS Identity:**
- Target: iPhone 8 (iPhone10,4 / D201AP)
- iOS Version: 16.7.16 (20H392)
- Kernel: XNU 8796 (Darwin 22)
- Display: 750×1334 logical → 1080×1920 physical (scaled)

## Architecture

```
Xiaomi ABL (bootloader, unlocked required)
  ↓
fastboot boot natrium-boot.img
  ↓
Boot shim (stops watchdog, loads payload)
  ↓
XNU kernel + Apple Device Tree + boot-args + firmware blobs
  ↓
UFS driver → HFS+ journaled root (in userdata partition)
  ↓
dyld → launchd → backboardd → SpringBoard
```

## Project Structure

```
ios-natrium/
├── xnu/                    # XNU kernel sources with patches
│   ├── patches/            # Kernel patches for Snapdragon 821
│   └── out/                # Built kernel output
├── shim/                   # Boot shim (bare-metal loader)
│   ├── src/                # C/assembly shim code
│   └── out/                # Compiled shim and boot image
├── drivers/                # Hardware drivers
│   ├── gpu/                # Adreno 530 GPU driver
│   ├── display/            # Display pipeline (MDP, panel)
│   ├── wifi/               # Wi-Fi (QCA6174)
│   ├── storage/            # UFS host controller
│   ├── touch/              # Touchscreen driver
│   └── keys/               # Power/volume buttons
├── tools/                  # Build and utilities scripts
│   ├── mkadt.py            # Generate Apple Device Tree
│   ├── mkpayload.py        # Assemble kernel payload
│   ├── mkiosroot.sh        # Create iOS root filesystem
│   └── xnu-build.sh        # Kernel build wrapper
├── firmware/               # Binary firmware blobs
│   ├── gpu/                # Adreno PM4/PFP microcode, zap shader
│   └── wifi/               # QCA6174 firmware files
├── dtb/                    # Device tree binaries
└── docs/                   # Documentation and guides
```

## Hardware Status

### Working
- ✅ Boot to SpringBoard
- ✅ Display (software rendering, 60 Hz)
- ✅ Touch input (Synaptics touchscreen)
- ✅ Physical buttons (power, volume)
- ✅ Storage (UFS with HFS+)
- ✅ Wi-Fi (QCA6174, WPA2-PSK)
- ✅ GPU (Adreno 530 with Metal-like acceleration)

### Not Working
- ❌ Cellular modem
- ❌ Bluetooth
- ❌ Audio (speaker, microphone)
- ❌ Camera
- ❌ Sensors (accelerometer, gyroscope, proximity, etc.)
- ❌ GPS
- ❌ Fingerprint sensor
- ❌ Calls, SMS, FaceTime
- ❌ Apple ID, App Store
- ❌ Data protection (no SEP)

## Requirements

### Hardware
- Xiaomi Mi 5s Plus with **unlocked bootloader** (mandatory)
- USB cable for fastboot
- At least 12 GB free in userdata partition

### Software (Build Host)
- Linux PC (Ubuntu 20.04+ or similar)
- macOS machine with Xcode 15.4 (for kernel compilation)
- Cross-compiler: `aarch64-linux-gnu-gcc`
- Python 3.8+
- Git
- `mkbootimg`, `fastboot`
- `hfsprogs`, `apfs-fuse`

## Build Instructions

### 1. Clone Repository
```bash
git clone https://github.com/YOUR_USERNAME/ios-natrium.git
cd ios-natrium
git submodule update --init --recursive
```

### 2. Download XNU Sources
```bash
cd xnu
git clone https://github.com/apple-oss-distributions/xnu.git -b xnu-8796.121.2 src
cd ..
```

### 3. Apply Patches
```bash
cd xnu/src
for patch in ../patches/*.patch; do
    patch -p1 < "$patch"
done
cd ../..
```

### 4. Build Kernel (on macOS with Xcode)
```bash
./tools/xnu-build.sh
```

### 5. Extract Firmware
```bash
# Extract from your Mi 5s Plus vendor partition
adb pull /vendor/firmware/a530_pfp.fw firmware/gpu/pfp.bin
adb pull /vendor/firmware/a530_pm4.fw firmware/gpu/pm4.bin
adb pull /vendor/firmware/a530_zap.mdt firmware/gpu/a530_zap.mdt
adb pull /vendor/firmware/a530_zap.b02 firmware/gpu/a530_zap.b02
# ... (QCA6174 Wi-Fi firmware)
```

### 6. Download iPhone 8 IPSW
```bash
# Download from https://ipsw.me/iPhone10,4
# Extract and create root filesystem
./tools/mkiosroot.sh iPhone_4.7_16.7.16_20H392_Restore.ipsw ios16-root.img
```

### 7. Generate Apple Device Tree
```bash
python3 tools/mkadt.py dtb/natrium.adt --natrium
```

### 8. Assemble Payload
```bash
python3 tools/mkpayload.py xnu/out/kernel.development.natrium \
    dtb/natrium.adt shim/out/payload.bin \
    --cmdline "debug=0xc cpus=4 rd=ufsroot0 rootrw=1 ufsrw=1 wdt=-1" \
    --blob GPU-PM4=firmware/gpu/pm4.bin \
    --blob GPU-PFP=firmware/gpu/pfp.bin \
    --blob GPU-ZAP-MDT=firmware/gpu/a530_zap.mdt \
    --blob GPU-ZAP-SEG=firmware/gpu/a530_zap.b02
```

### 9. Build Boot Image
```bash
cd shim
./build.sh
cd ..
```

### 10. Flash and Boot
```bash
# Flash root filesystem (WARNING: erases userdata)
fastboot flash userdata ios16-root.img

# Boot (temporary, does not modify boot partition)
fastboot boot shim/out/natrium-boot.img
```

## Boot Arguments

```
debug=0xc serial=3 cpus=4 keepsyms=1
rd=ufsroot0 ufsrw=1 rootrw=1
cs_enforcement_disable=1 amfi_get_out_of_my_way=1
wdt=-1
launchd_unsecure_cache=1
natriumfb_hz=60
```

## Technical Details

### Memory Layout
- Physical RAM: 0x80000000 - 0x17FFFFFFF (4 GB total, upper 3 GB used)
- Kernel load: 0x100000000 (upper DDR bank, above TrustZone)
- 16K page size (ARM64 4-level tables)

### Display Pipeline
- Logical: 750×1334 @ 326 PPI (iPhone 8)
- Physical: 1080×1920 @ 386 PPI (Mi 5s Plus)
- Bilinear scaling via GPU or software
- 60 Hz refresh through MDP5

### Security Model
- **PERMISSIVE MODE**: Code signing disabled, sandbox policies relaxed
- Stub AppleKeyStore (no data protection)
- No Secure Enclave (SEP)
- Boot arg: `cs_enforcement_disable=1`
- ⚠️ **DO NOT use for personal data or production**

## Known Issues

1. **First boot takes 4-5 minutes** - dyld cache generation
2. **No thermal management** - device may get hot under load
3. **No deep sleep** - battery drain when idle
4. **Touch calibration** - may need adjustments
5. **Wi-Fi stability** - occasional disconnections
6. **Limited app compatibility** - App Store unavailable

## References

This project builds upon:
- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) - Original XNU on Android concept
- [joranv1/ios-sailfish](https://github.com/joranv1/ios-sailfish) - Snapdragon 821 reference port
- [HackiOS Project](https://hackiosx.dev/) - iOS on Android initiative
- [Apple Open Source](https://opensource.apple.com/) - XNU kernel sources

## Legal Notice

- XNU kernel: Apple Public Source License 2.0
- iOS userland: **Proprietary Apple software** (personal backup use only)
- This is a research project - **not for piracy or circumvention**
- Requires legitimate iOS device and IPSW from Apple
- No Apple proprietary code is redistributed in this repository

## Disclaimer

⚠️ **Warning**: This project modifies your device at a low level. Proceed at your own risk:
- May brick your device
- Voids warranty
- Requires unlocked bootloader
- Erases all data
- No cellular connectivity
- For educational purposes only

## License

- Project code: MIT License
- XNU kernel: Apple Public Source License 2.0
- Build tools: Various open source licenses (see individual files)

---

**Status**: 🚧 Work in Progress - Not yet bootable

Made with reference to the HackiOS community's groundbreaking work.
