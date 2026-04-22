# Signing Rockchip Images

This guide explains how to sign Rockchip firmware images outside the browser, burn the public key hash for signed boot, and then use this web rkdeveloptool wrapper to flash the signed images.

Read this first: OTP/eFuse programming is usually irreversible. Test this on disposable hardware before doing it on production boards. Keep the private key offline and backed up. If you lose the signing key after enabling signed boot, you may not be able to boot new firmware on those devices.

## What This Project Does

The browser page flashes images over WebUSB. It does not currently sign images in the browser and it does not expose raw eFuse writes.

The intended flow is:

1. Clone this repository on a Linux host.
2. Generate an RSA signing key pair.
3. Put your unsigned Rockchip images in a signing input directory.
4. Run Rockchip's `fit-sign.sh` tool from `rkbin/tools`.
5. Use `--burn-key-hash` when you intentionally want the signed SPL/loader to program the public key hash into OTP/eFuse on supported platforms.
6. Open the web page and flash the signed output images.
7. Power-cycle and verify that signed images boot, and unsigned or wrong-key images do not.

## Host Requirements

The bundled Rockchip signing tools under `rkbin/tools` are Linux x86-64 binaries. On macOS or Windows, use a Linux machine, VM, container, or WSL2.

Install common host tools:

```sh
sudo apt-get update
sudo apt-get install -y git openssl file device-tree-compiler python3 coreutils
```

Clone the repository:

```sh
git clone --recurse-submodules https://github.com/asadm/rkdeveloptool.git
cd rkdeveloptool
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

## Prepare Keys

Rockchip's `fit-sign.sh` expects these files in the key directory:

- `dev.key`: private RSA key
- `dev.pubkey`: public RSA key for `rk_sign_tool`
- `dev.crt`: certificate used by U-Boot `mkimage`

Generate RSA-2048 keys:

```sh
mkdir -p keys
openssl genrsa -out keys/dev.key 2048
openssl rsa -in keys/dev.key -pubout -out keys/dev.pubkey
openssl req -batch -new -x509 -key keys/dev.key -out keys/dev.crt
chmod 600 keys/dev.key
```

If your platform's `fit_signcfg/sign.readonly_config` enables RSA-4096 with `CONFIG_FIT_ENABLE_RSA4096_SUPPORT=y`, generate a 4096-bit key instead:

```sh
mkdir -p keys
openssl genrsa -out keys/dev.key 4096
openssl rsa -in keys/dev.key -pubout -out keys/dev.pubkey
openssl req -batch -new -x509 -key keys/dev.key -out keys/dev.crt
chmod 600 keys/dev.key
```

Do not commit `keys/dev.key`.

## Prepare Unsigned Images

Create a source directory containing the unsigned images and the Rockchip signing config:

```sh
mkdir -p sign-src sign-out
cp /path/to/firmware/*.img sign-src/
cp /path/to/loader-or-download.bin sign-src/
cp -r /path/to/fit_signcfg sign-src/
```

The source directory normally contains files such as:

```text
sign-src/
  boot.img
  rootfs.img
  uboot.img
  idblock.img
  MiniLoaderAll.bin or *_loader*.bin or *download*.bin
  fit_signcfg/
    MINIALL.ini
    sign.readonly_config
```

`fit_signcfg` usually comes from the Rockchip BSP or SDK build output. The signing script requires it because it contains SoC, SPL, U-Boot, FIT, rollback, and crypto settings. If this directory is missing, get it from the exact BSP/SDK build that produced your loader and `uboot.img`.

Important: `fit-sign.sh` signs FIT images. Files such as `boot.img` and some `rootfs.img` outputs may be FIT images, but a plain ext4, squashfs, ubifs, or raw rootfs image is not automatically protected by this script. If your root filesystem is not inside a FIT image, protect it with your platform's verified boot mechanism, such as dm-verity, AVB, a signed initramfs policy, or another board-specific rootfs verification path.

## Sign Images

Basic signing:

```sh
./rkbin/tools/fit-sign.sh \
  --key-dir keys \
  --src-dir sign-src \
  --out-dir sign-out
```

With image versions:

```sh
./rkbin/tools/fit-sign.sh \
  --key-dir keys \
  --src-dir sign-src \
  --out-dir sign-out \
  --version uboot.img 1 boot.img 1 rootfs.img 1
```

With rollback indexes, if your `sign.readonly_config` enables rollback protection:

```sh
./rkbin/tools/fit-sign.sh \
  --key-dir keys \
  --src-dir sign-src \
  --out-dir sign-out \
  --version uboot.img 1 boot.img 1 rootfs.img 1 \
  --rollback-index uboot.img 1 boot.img 1 rootfs.img 1
```

The script will place signed files in `sign-out/`. Depending on the platform, this can include:

```text
sign-out/
  uboot.img
  boot.img
  rootfs.img
  idblock.img
  MiniLoaderAll.bin or *_loader*.bin or *download*.bin
```

## Burn the Public Key Hash

For supported Rockchip SPL configurations, use `--burn-key-hash`:

```sh
./rkbin/tools/fit-sign.sh \
  --key-dir keys \
  --src-dir sign-src \
  --out-dir sign-out \
  --burn-key-hash \
  --version uboot.img 1 boot.img 1 rootfs.img 1 \
  --rollback-index uboot.img 1 boot.img 1 rootfs.img 1
```

What this does:

- Adds the signing public key into the SPL/U-Boot verification data.
- Sets `burn-key-hash = <1>` in the SPL DTB.
- Signs and repacks the loader/idblock/uboot/FIT images.
- On supported platforms, the signed SPL/loader programs the RSA public key hash into OTP/eFuse when it runs.

The script requires `CONFIG_SPL_FIT_HW_CRYPTO=y` for `--burn-key-hash`. If it fails with that error, your current SPL/config does not support this burn flow.

Do not add a browser feature that writes arbitrary eFuse bytes unless you also implement the SoC-specific OTP layout, locking rules, readback, and recovery behavior. This repository currently avoids raw eFuse writes by using the Rockchip signed SPL flow.

## Flash Signed Images With the Web Page

Serve the web UI:

```sh
cd docs
python3 -m http.server 8000
```

Open Chrome or another Chromium-based browser:

```text
http://localhost:8000/
```

Then:

1. Put the board in Maskrom mode.
2. Click `Select device`.
3. Drop the signed files from `sign-out/` into the page.
4. Include the signed loader or download binary.
5. Click `Start flash`.
6. Wait for flashing to finish.
7. Power-cycle the board.

If you used `--burn-key-hash`, the first boot of the signed SPL/loader is the point where supported platforms burn the public key hash. Keep power stable during this step.

## Verify Signed Boot

Use at least these checks:

1. Flash the signed output images and confirm the board boots normally.
2. Rebuild or sign a new image with the same key and confirm updates still boot.
3. Try an image signed with a different key and confirm the board refuses to boot it.
4. Try an unsigned image and confirm the board refuses to boot it.

The web page can still write bytes to flash if the device is in a writable loader mode. Signed boot verification happens when the boot ROM, SPL, or U-Boot loads the next stage. A write succeeding does not prove the image is trusted. The real proof is that the device boots signed images and refuses unsigned or wrong-key images after reset.

For production, also verify from Linux if your kernel exposes the relevant status. Some Rockchip BSPs expose secure boot state or public key hash through kernel drivers, sysfs, debugfs, procfs, or vendor tools. The exact path is BSP and SoC specific.

## Recovery Notes

- Keep one unfused development board for testing new signing flows.
- Keep the private key and exact signing config used for production.
- Keep unsigned originals and signed outputs archived with build metadata.
- Do not enable rollback indexes until your update process can monotonically manage them.
- If a fused board stops booting, recovery usually requires signing a known-good image with the same private key. On some configurations, Maskrom access may still allow flashing but not booting unsigned code.

## Troubleshooting

`ERROR: No fit_signcfg directory`

Copy `fit_signcfg/` from the Rockchip BSP/SDK output that produced your firmware.

`ERROR: CONFIG_SPL_FIT_SIGNATURE is disabled`

Your SPL was not built with FIT signature verification. Rebuild the loader/SPL/U-Boot with signed boot support enabled.

`ERROR: --burn-key-hash requires CONFIG_SPL_FIT_HW_CRYPTO=y`

The current SPL config does not support the Rockchip hardware key-hash burn path used by this script.

`ERROR: Wrong rsa 'algo' in its file`

Your key size does not match the FIT configuration. Use RSA-2048 unless `CONFIG_FIT_ENABLE_RSA4096_SUPPORT=y` is set.

The board flashes but does not boot after key burn

Confirm every boot-stage image was signed by the same key and generated from matching `fit_signcfg`/BSP artifacts. Also check rollback indexes if rollback protection is enabled.

