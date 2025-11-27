# Rockchip upgrade_tool → rkdeveloptool notes

This documents how we reproduced `upgrade_tool UF` using only `rkdeveloptool` and an `update.img`, plus the artifacts and offsets we derived.

## Key findings
- `update.img` is a two-layer Rockchip container. Unpacking yields a loader (`boot.bin`/`bootloader.bin`) and `firmware.img`; unpacking `firmware.img` yields partition images, `package-file`, and `download.bin`.
- `download.bin` is RC4/signed; stock `afptool` fails (“invalid tag”). `upgrade_tool EXF` can decrypt it and emit usable images.
- `upgrade_tool` stages: load RAM loader → probe flash → prepare IDB → write IDB/loader → flash partitions → reset.
- Partition layout recovered from image strings (mtdparts):
  ```
  mtdparts=spi-nand0:256K(env),256K@256K(idblock),512K(uboot),4M(boot),30M(oem),10M(userdata),210M(rootfs)
  ```
- LBA offsets (512-byte sectors):
  - env: start 0, size 256K → 512 sectors
  - idblock: start 512, size 256K → 512 sectors
  - uboot: start 1024, size 512K → 1024 sectors
  - boot: start 2048, size 4M → 8192 sectors
  - oem: start 10240, size 30M → 61440 sectors
  - userdata: start 71680, size 10M → 20480 sectors
  - rootfs: start 92160, size 210M → 430080 sectors
- `rkdeveloptool ppt` needs a parameter/GPT provisioned; since `parameter.txt` was not available via `afptool`, we used raw `wl` with explicit LBAs.

## Extraction steps
1) `./upgrade_tool EXF update.img out/`
   - Outputs: `out/bootloader.bin` (loader/IDB) and partition images: `env.img`, `idblock.img`, `uboot.img`, `boot.img`, `oem.img`, `userdata.img`, `rootfs.img`, `package-file`, `download.bin`.
2) `download.bin` remained encrypted; we did not recover `parameter.txt` from it with stock `afptool`.

## Flash flow with rkdeveloptool (no parameter.txt)
Assumes Maskrom mode and `rkdeveloptool` in PATH.

```bash
# Check Maskrom
rkdeveloptool ld

# Load RAM loader, switch to Loader
rkdeveloptool db out/bootloader.bin
rkdeveloptool ld   # should show Loader

# Write loader/IDB to flash
rkdeveloptool ul out/bootloader.bin

# Flash partitions by LBA (512-byte sectors)
rkdeveloptool wl 0      out/env.img
rkdeveloptool wl 512    out/idblock.img
rkdeveloptool wl 1024   out/uboot.img
rkdeveloptool wl 2048   out/boot.img
rkdeveloptool wl 10240  out/oem.img
rkdeveloptool wl 71680  out/userdata.img
rkdeveloptool wl 92160  out/rootfs.img

# Reboot
rkdeveloptool rd
```

## Validation
- After flashing, the device booted and appeared over ADB (`adb shell` worked).
- Erase messages during `wl` are normal for UBI-backed regions on NAND.

## If parameter.txt is needed
- Either extract from a running device’s bootargs (`/proc/cmdline`), or use a tool/script that can decrypt `download.bin` (same key as `upgrade_tool`). With `parameter.txt` available, you could replace the raw `wl` calls with `prm` and `wlx` by partition name.

### Finding parameter values when decryption fails
- From a booted device: `adb shell cat /proc/cmdline` (or `/proc/device-tree/chosen/bootargs`) often contains the `mtdparts` string; copy it into a parameter file.
- From images: run `strings` across extracted blobs to find `mtdparts`/`CMDLINE` (e.g. `strings -n 4 *.img | rg -i 'mtdparts|cmdline|parameter'`), then convert offsets to 512-byte LBAs (size/start in bytes ÷ 512).
- From flash directly (if booted): `rkdeveloptool rfi` for geometry, then `rkdeveloptool rl <small range>` to read early sectors; `strings` on the dump may reveal the parameter string embedded in IDB/uboot.
- Once you have the `mtdparts`, compute LBAs: start_byte/512 for the “@offset” values, size_byte/512 for lengths, or derive starts cumulatively from the size list.
- How we recovered it here: `strings -n 4 *.img | rg -i 'mtdparts|cmdline|parameter'` over the extracted `env/uboot/rootfs` blobs exposed `mtdparts=spi-nand0:256K(env),256K@256K(idblock),512K(uboot),4M(boot),30M(oem),10M(userdata),210M(rootfs)`, which we then converted to the LBA table above.
