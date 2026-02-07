# Building JARVIS Kernel for Pi 4

## Quick Build (Recommended)

```bash
# In WSL
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
./build_and_copy_kernel.sh
```

This script will:
1. Check TII seL4 workspace exists
2. Create JARVIS project if needed
3. Configure CMake for Pi 4 (BCM2711)
4. Build kernel using Ninja
5. Copy kernel8.img to `phase2/firmware/`

## Manual Build Steps

If the automated script doesn't work, follow these steps:

### 1. Navigate to Build Directory

```bash
cd ~/sel4-workspace/rpi4_jarvis
```

### 2. Configure Build

```bash
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    ../projects/jarvis-sel4
```

### 3. Build

```bash
ninja
```

### 4. Copy Kernel

```bash
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/
```

## Verify Build

```bash
# Check kernel size (should be ~700KB)
ls -lh ~/sel4-workspace/rpi4_jarvis/images/kernel8.img

# Verify it's ARM64
file ~/sel4-workspace/rpi4_jarvis/images/kernel8.img
# Should show: "ARM aarch64"
```

## Troubleshooting

### CMake Configuration Fails

- Check TII seL4 workspace is synced: `cd ~/sel4-workspace && repo sync`
- Verify JARVIS project exists: `ls ~/sel4-workspace/projects/jarvis-sel4`

### Build Fails

- Check compiler: `aarch64-linux-gnu-gcc --version`
- Check CMake output for specific errors
- Verify all source files exist in `~/sel4-workspace/projects/jarvis-sel4/src/`

### Kernel Too Small

If kernel8.img is < 100KB, the build didn't complete properly:
- Check build logs for errors
- Verify all source files compiled
- Try clean build: `rm -rf ~/sel4-workspace/rpi4_jarvis && mkdir ~/sel4-workspace/rpi4_jarvis`


